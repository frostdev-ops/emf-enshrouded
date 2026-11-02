// emf_content.dll (src/content, Rust around EML's mod loader), loaded the way EMF will load it: LoadLibrary, one or two
// builds, FreeLibrary. The DLL must export only its two functions, answer every request with JSON, survive small
// buffers, non-ASCII folders and repeated use, and leave no thread behind. Built only when CMake has EMF_CONTENT on.
#ifdef EMF_CONTENT_DLL

#include <windows.h>

#include <tlhelp32.h>

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common/pe_image.h"
#include "content/emf_content.h"
#include "core/json.h"
#include "loader/module_threads.h"
#include "test_scratch.h"

#pragma comment(lib, "version.lib")  // GetFileVersionInfoW: the version resource test

namespace {

namespace json = emf::core::json;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// A path CMake wrote (forward slashes) as a Windows path.
std::wstring native(const char* cmake_path) {
    std::wstring p = widen(cmake_path);
    for (wchar_t& c : p)
        if (c == L'/') c = L'\\';
    return p;
}

std::wstring dll_path() { return native(EMF_CONTENT_DLL); }

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string request_for(const std::wstring& dir) { return "{\"game_dir\": " + json_string(utf8(dir)) + "}"; }

// A folder in %TEMP% for one test, removed at the end of the scope (EML writes mods\ and .cache\ into it).
struct Folder {
    std::wstring path;
    std::wstring root;  // what is removed: path, or the folder that holds it
    explicit Folder(const std::wstring& stem) : path(test_scratch::temp_path(stem)) {
        std::filesystem::create_directories(path);
    }
    // <stem>_<run tag>\<name>: a folder with exactly that name.
    Folder(const std::wstring& stem, const std::wstring& name) : Folder(stem) {
        root = path;
        path += L"\\" + name;
        std::filesystem::create_directories(path);
    }
    ~Folder() {
        std::error_code ec;
        std::filesystem::remove_all(root.empty() ? path : root, ec);
    }
    // An exe EML cannot read types from: EML's own code runs on this folder and fails.
    void add_fake_exe() const { std::ofstream(std::filesystem::path(path) / L"enshrouded.exe") << "MZ but not a game"; }
};

struct LogLines {
    std::vector<std::pair<int, std::string>> lines;
    static void collect(void* ctx, int level, const char* line) {
        static_cast<LogLines*>(ctx)->lines.emplace_back(level, line);
    }
};

struct Result {
    int32_t code = -1;
    std::string reply;  // without the NUL
    size_t len = 0;
};

// The DLL, loaded for the life of the object.
struct ContentDll {
    HMODULE module = LoadLibraryW(dll_path().c_str());
    emf_content_abi_fn abi =
        module ? reinterpret_cast<emf_content_abi_fn>(GetProcAddress(module, "emf_content_abi")) : nullptr;
    emf_content_build_fn build_fn =
        module ? reinterpret_cast<emf_content_build_fn>(GetProcAddress(module, "emf_content_build")) : nullptr;

    ContentDll() = default;
    ContentDll(const ContentDll&) = delete;
    ContentDll& operator=(const ContentDll&) = delete;
    ~ContentDll() {
        if (module) FreeLibrary(module);
    }
    bool unload() {
        const bool ok = module && FreeLibrary(module);
        module = nullptr;
        return ok;
    }

    Result build(const std::string& request, size_t cap = 64 * 1024, LogLines* log = nullptr) const {
        Result r;
        std::vector<char> buf(cap, '#');
        r.code = build_fn(request.c_str(), log ? &LogLines::collect : nullptr, log, cap ? buf.data() : nullptr, cap,
                          &r.len);
        if (r.code != EMF_CONTENT_REPLY_TOO_SMALL && r.len > 0 && r.len <= cap) r.reply.assign(buf.data(), r.len - 1);
        return r;
    }
};

json::Value parse_reply(const Result& r) {
    auto parsed = json::parse(r.reply);
    INFO("reply: " << r.reply);
    REQUIRE(parsed.value.has_value());
    return *parsed.value;
}

std::string message_of(const json::Value& reply) {
    const json::Value* m = reply.get("message");
    return m && m->is(json::Value::Type::String) ? m->s : std::string();
}

// What a DLL file's export and import directories hold. The file is mapped as an image apart from any loaded copy,
// and unmapped again.
struct PeTables {
    bool mapped = false;
    std::set<std::string> exports;  // names
    DWORD export_functions = 0;     // NumberOfFunctions: named and ordinal-only exports
    std::set<std::string> imports;  // DLL names in lower case, delay-loaded ones too
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

PeTables pe_tables(const std::wstring& path) {
    PeTables t;
    const uintptr_t base = emf::map_image_file(path.c_str());
    if (!base) return t;
    t.mapped = true;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& exp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exp_dir.VirtualAddress != 0) {
        const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + exp_dir.VirtualAddress);
        t.export_functions = exp->NumberOfFunctions;
        const auto* name_rvas = reinterpret_cast<const DWORD*>(base + exp->AddressOfNames);
        for (DWORD i = 0; i < exp->NumberOfNames; ++i)
            t.exports.insert(reinterpret_cast<const char*>(base + name_rvas[i]));
    }
    const IMAGE_DATA_DIRECTORY& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.VirtualAddress != 0)
        for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imp_dir.VirtualAddress); d->Name; ++d)
            t.imports.insert(lower(reinterpret_cast<const char*>(base + d->Name)));
    const IMAGE_DATA_DIRECTORY& delay_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (delay_dir.VirtualAddress != 0)
        for (auto* d = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + delay_dir.VirtualAddress);
             d->DllNameRVA; ++d)
            t.imports.insert(lower(reinterpret_cast<const char*>(base + d->DllNameRVA)));
    UnmapViewOfFile(reinterpret_cast<void*>(base));
    return t;
}

int thread_count() {
    const DWORD pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid) ++n;
    CloseHandle(snap);
    return n;
}

void read_all(HANDLE pipe, std::string& into) {
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got > 0) into.append(buf, got);
}

struct ProcessResult {
    DWORD code = 0xFFFFFFFF;  // also when it did not start or did not end within a minute
    std::string out, err;
};

// Runs `exe args` without a window; its stdout and stderr come back through pipes (no files to clean up).
ProcessResult run_capture(const std::wstring& exe, const std::wstring& args) {
    ProcessResult r;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return r;
    if (!CreatePipe(&err_r, &err_w, &sa, 0)) {
        CloseHandle(out_r);
        CloseHandle(out_w);
        return r;
    }
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &si, &pi);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (started) {
        std::thread err_reader([&] { read_all(err_r, r.err); });
        read_all(out_r, r.out);
        err_reader.join();
        if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &r.code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(out_r);
    CloseHandle(err_r);
    return r;
}

}  // namespace

TEST_CASE("content dll: exports only the two functions") {
    const std::set<std::string> expected{"emf_content_abi", "emf_content_build"};
    const PeTables pe = pe_tables(dll_path());
    REQUIRE(pe.mapped);
    std::string listed;
    for (const auto& n : pe.exports) listed += n + " ";
    INFO("exports: " << listed);
    CHECK(pe.exports == expected);
    CHECK(pe.export_functions == 2);  // no export by ordinal only either
}

TEST_CASE("content dll: imports only Windows' own DLLs") {
    // A static CRT and no other DLL: nothing to install, and nothing that stays loaded after FreeLibrary.
    const std::set<std::string> allowed{"kernel32.dll", "ntdll.dll", "bcryptprimitives.dll",
                                        "api-ms-win-core-synch-l1-2-0.dll"};
    const PeTables pe = pe_tables(dll_path());
    REQUIRE(pe.mapped);
    CHECK_FALSE(pe.imports.empty());
    for (const auto& name : pe.imports) {
        INFO("import: " << name);
        CHECK(allowed.count(name) == 1);
    }
}

TEST_CASE("content dll: abi is 1") {
    ContentDll dll;
    REQUIRE(dll.module != nullptr);
    REQUIRE(dll.abi != nullptr);
    CHECK(dll.abi() == 1u);
    CHECK(dll.abi() == EMF_CONTENT_ABI);
}

// The licence (THIRD_PARTY_NOTICES.md, option A): Explorer's file properties name the DLL, EMF's version and the GPL.
TEST_CASE("content dll: its version resource names it, EMF's version and the GPL") {
    const std::wstring path = dll_path();
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    REQUIRE(size > 0);
    std::vector<char> info(size);
    REQUIRE(GetFileVersionInfoW(path.c_str(), 0, size, info.data()));
    const auto value = [&](const wchar_t* name) {
        void* text = nullptr;
        UINT len = 0;
        const std::wstring key = std::wstring(L"\\StringFileInfo\\040904b0\\") + name;
        if (!VerQueryValueW(info.data(), key.c_str(), &text, &len) || !text || len == 0) return std::string();
        return utf8(std::wstring(static_cast<const wchar_t*>(text), len - 1));  // len counts the terminating zero
    };
    CHECK(value(L"FileDescription") == "EMF content builder for EML mods");
    CHECK(value(L"LegalCopyright") == "Copyright (c) EML contributors and EMF contributors. GPL-3.0-or-later.");
    CHECK(value(L"FileVersion") == EMF_VERSION);
    CHECK(value(L"OriginalFilename") == "emf_content.dll");
}

TEST_CASE("content dll: an empty folder fails with a message and no crash") {
    Folder folder(L"emf_content_empty");
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    const Result r = dll.build(request_for(folder.path));
    CHECK(r.code != EMF_CONTENT_OK);
    const json::Value reply = parse_reply(r);
    REQUIRE(reply.get("code") != nullptr);
    CHECK(reply.get("code")->n == r.code);
    CHECK_FALSE(message_of(reply).empty());
}

TEST_CASE("content dll: too small a reply buffer") {
    Folder folder(L"emf_content_small");
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    const std::string request = request_for(folder.path);
    const Result big = dll.build(request);

    std::vector<char> buf(8, '#');
    size_t len = 0;
    const int32_t code = dll.build_fn(request.c_str(), nullptr, nullptr, buf.data(), buf.size(), &len);
    CHECK(code == EMF_CONTENT_REPLY_TOO_SMALL);
    CHECK(len > 8);
    CHECK(std::string(buf.data(), buf.size()) == "########");

    const Result retry = dll.build(request, len);
    CHECK(retry.code == big.code);
    CHECK(retry.len == len);
    CHECK(parse_reply(retry).get("code")->n == big.code);
}

TEST_CASE("content dll: non-ASCII folder") {
    Folder folder(L"emf_content_utf8", L"Enshrouded ø test (x86)");
    const std::string name8 = utf8(folder.path);
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);

    // Before EML runs: the shim's own message names the folder, in UTF-8.
    const Result empty = dll.build(request_for(folder.path));
    CHECK(empty.code != EMF_CONTENT_OK);
    CHECK(message_of(parse_reply(empty)).find(name8) != std::string::npos);

    // EML itself gets the path: it makes mods\ in exactly that folder, and its log lines name the exe there.
    folder.add_fake_exe();
    LogLines log;
    const Result eml = dll.build(request_for(folder.path), 64 * 1024, &log);
    CHECK(eml.code == EMF_CONTENT_RUN_FAILED);
    CHECK(std::filesystem::is_directory(std::filesystem::path(folder.path) / L"mods"));
    bool named = false;
    for (const auto& [level, line] : log.lines) {
        // EML writes paths with Debug quoting (doubled backslashes); compare with single ones.
        std::string single;
        for (size_t i = 0; i < line.size(); ++i) {
            single += line[i];
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '\\') ++i;
        }
        if (single.find(name8 + "\\enshrouded.exe") != std::string::npos) named = true;
    }
    CHECK(named);
}

TEST_CASE("content dll: two builds in one load") {
    Folder folder(L"emf_content_twice");
    folder.add_fake_exe();
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    LogLines first, second;
    const Result a = dll.build(request_for(folder.path), 64 * 1024, &first);
    CHECK(emf::loader::threads_started_in(dll.module).empty());
    const Result b = dll.build(request_for(folder.path), 64 * 1024, &second);
    CHECK(emf::loader::threads_started_in(dll.module).empty());
    CHECK(a.code == EMF_CONTENT_RUN_FAILED);
    CHECK(b.code == a.code);
    CHECK(message_of(parse_reply(b)) == message_of(parse_reply(a)));
    CHECK(first.lines.size() > 1);
    CHECK(second.lines.size() == first.lines.size());
}

TEST_CASE("content dll: 50 load-build-unload cycles leave no threads") {
    Folder folder(L"emf_content_cycles");
    folder.add_fake_exe();
    const std::string request = request_for(folder.path);
    {
        ContentDll warm;  // the first load may start the loader's own worker threads; they are not the DLL's
        REQUIRE(warm.build_fn != nullptr);
        warm.build(request);
    }
    const int before = thread_count();
    REQUIRE(before > 0);
    for (int i = 0; i < 50; ++i) {
        ContentDll dll;
        REQUIRE(dll.build_fn != nullptr);
        CHECK(dll.build(request).code == EMF_CONTENT_RUN_FAILED);
        CHECK(emf::loader::threads_started_in(dll.module).empty());  // nothing left running in the DLL's code
        CHECK(dll.unload());
        CHECK(GetModuleHandleW(L"emf_content.dll") == nullptr);
    }
    // Windows' loader workers end on their own after a few idle seconds; a thread the DLL started would never end.
    int after = thread_count();
    for (int waited = 0; after > before && waited < 100; ++waited) {
        Sleep(100);
        after = thread_count();
    }
    CHECK(after <= before);
}

TEST_CASE("content dll: a panic gives PANIC, and its message and place reach the log") {
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    LogLines log;
    const Result r = dll.build("{\"game_dir\": \"unused\", \"test_panic\": \"boom for the test\"}", 64 * 1024, &log);
    CHECK(r.code == EMF_CONTENT_PANIC);
    CHECK(message_of(parse_reply(r)).find("boom for the test") != std::string::npos);
    bool logged = false;
    for (const auto& [level, line] : log.lines) {
        const bool names_it = line.find("boom for the test") != std::string::npos;
        if (level == 0 && names_it && line.find("lib.rs:") != std::string::npos) logged = true;
    }
    CHECK(logged);
    // and the DLL goes on working
    Folder folder(L"emf_content_after_panic");
    CHECK(dll.build(request_for(folder.path)).code == EMF_CONTENT_RUN_FAILED);
}

TEST_CASE("content dll: a reply cut to fit the buffer keeps the real code") {
    Folder folder(L"emf_content_cut");
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    std::vector<char> buf(120, '#');
    size_t len = 0;
    const std::string request = request_for(folder.path);
    const int32_t code = dll.build_fn(request.c_str(), nullptr, nullptr, buf.data(), buf.size(), &len);
    CHECK(code == EMF_CONTENT_RUN_FAILED);
    CHECK(len > buf.size());
    const auto parsed = json::parse(std::string(buf.data()));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->get("code")->n == EMF_CONTENT_RUN_FAILED);
    REQUIRE(parsed.value->get("truncated") != nullptr);
    CHECK(parsed.value->get("truncated")->b);
}

TEST_CASE("content dll: log lines arrive") {
    Folder folder(L"emf_content_log");
    ContentDll dll;
    REQUIRE(dll.build_fn != nullptr);
    LogLines log;
    dll.build(request_for(folder.path), 64 * 1024, &log);
    CHECK_FALSE(log.lines.empty());
    for (const auto& [level, line] : log.lines) {
        CHECK(level >= 0);
        CHECK(level <= 3);
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.find('\r') == std::string::npos);
    }
}

TEST_CASE("emf_content_run: empty folder gives a non-zero exit and JSON on stdout") {
    Folder folder(L"emf_content_run_empty");
    const ProcessResult r = run_capture(native(EMF_CONTENT_RUN_EXE),L"\"" + folder.path + L"\"");
    INFO("stdout: " << r.out << "\nstderr: " << r.err);
    CHECK(r.code != 0);
    CHECK(r.code != 0xFFFFFFFF);
    const auto parsed = json::parse(r.out);
    REQUIRE(parsed.value.has_value());
    REQUIRE(parsed.value->get("code") != nullptr);
    CHECK(parsed.value->get("code")->n == static_cast<double>(r.code));
    CHECK(message_of(*parsed.value).find(utf8(folder.path)) != std::string::npos);
    CHECK(r.err.find("[error] ") != std::string::npos);  // log lines go to stderr as "[level] line"
}

TEST_CASE("emf_content_run: --repeat loads, builds and unloads N times") {
    Folder folder(L"emf_content_run_repeat");
    folder.add_fake_exe();
    const ProcessResult r = run_capture(native(EMF_CONTENT_RUN_EXE), L"\"" + folder.path + L"\" --repeat 3");
    INFO("stdout: " << r.out << "\nstderr: " << r.err);
    CHECK(r.code == static_cast<DWORD>(EMF_CONTENT_RUN_FAILED));
    int replies = 0;
    size_t at = 0;
    for (size_t nl; (nl = r.out.find('\n', at)) != std::string::npos; at = nl + 1) {
        const auto parsed = json::parse(r.out.substr(at, nl - at));
        REQUIRE(parsed.value.has_value());
        CHECK(parsed.value->get("code")->n == EMF_CONTENT_RUN_FAILED);
        ++replies;
    }
    CHECK(replies == 3);
    CHECK(r.err.find("still running") == std::string::npos);
}

#endif  // EMF_CONTENT_DLL
