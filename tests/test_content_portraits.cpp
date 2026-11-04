// The loader's portraits call into emf_content.dll (src/loader/content_portraits.h): load, check the ABI, call
// emf_content_portraits once, unload; the reply's items as drawn, current or failed. The DLL here is the stand-in
// (tools/standin_content), which writes <cache>\<key>\00.png and index.json and fails the key "broken".
#include <doctest.h>

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

#include "loader/content_portraits.h"
#include "loader/portraits_plan.h"
#include "test_scratch.h"

namespace {
std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    const std::wstring exe(buf, n);
    return exe.substr(0, exe.find_last_of(L'\\'));
}
}  // namespace

TEST_CASE("content portraits: the stand-in draws the items, the broken one fails, and the DLL is unloaded after") {
    const std::wstring cache = test_scratch::temp_path(L"emf_portraits_cache");
    test_scratch::remove_at_exit(cache);
    emf::loader::PortraitItem wolf, broken;
    wolf.key = "wolf";
    wolf.template_guid = "503fa1d1-dce2-45a1-85a5-185a22666d33";
    broken.key = "broken";
    broken.template_guid = wolf.template_guid;
    std::vector<std::string> lines;
    const emf::loader::BuildLog log = [&](int, const std::string& l) { lines.push_back(l); };
    const std::wstring dll = exe_dir() + L"\\standin_content\\emf_content.dll";
    const auto out = emf::loader::run_portraits(dll, emf::loader::portraits_request(L"C:\\Game", cache, {wolf, broken}),
                                                log);
    CHECK(out.call.code == 1);  // EMF_CONTENT_ITEMS_FAILED: some items failed, the others are drawn
    CHECK(out.call.unloaded);
    REQUIRE(out.drawn.size() == 1u);
    CHECK(out.drawn[0] == "wolf");
    REQUIRE(out.failed.size() == 1u);
    CHECK(out.failed[0] == "broken: broken on purpose");
    CHECK(std::filesystem::exists(cache + L"\\wolf\\index.json"));
    CHECK(lines.size() == 2u);  // the stand-in logs one line per item
    std::error_code ec;
    std::filesystem::remove_all(cache, ec);
}

TEST_CASE("content portraits: a DLL with another ABI is refused, a missing one is an outcome, never an exception") {
    const auto wrong = emf::loader::run_portraits(exe_dir() + L"\\standin_content_abi2\\emf_content.dll", "{}", nullptr);
    CHECK(wrong.call.code == emf::loader::kBuildWrongAbi);
    CHECK(wrong.call.unloaded);
    const auto none = emf::loader::run_portraits(exe_dir() + L"\\no_such\\emf_content.dll", "{}", nullptr);
    CHECK(none.call.code == emf::loader::kBuildNoDll);
}
