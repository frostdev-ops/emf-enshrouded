// EMF.exe's "Install a downloaded file" (app/install.h): the plan for each kind of archive, applying it (never
// overwriting, an update moving the old folder to emf\backup\, a failure taking everything back), the record with
// SHA-256s, uninstalling exactly what was installed, and the lines it adds to and takes out of dinput8-chain.ini.
#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>

#include "app/archive.h"
#include "app/game_folder.h"
#include "app/install.h"
#include "app/json_out.h"
#include "app/text.h"
#include "app_archive_files.h"

using namespace emf::app;
using app_test::TempGame;
using Action = PlanPart::Action;

namespace {

const char* kTools = "creative | Creative Mode | text:creative-mode-marker\n"
                     "eml | EML (Enshrouded Mod Loader) | text:EML_LOG_FILE_PATH\n"
                     "shroudforge | ShroudForge | text:shroudforge | beside:shroudforge | same-loader:dbghelp.dll\n"
                     "greeneye | Greeneye Loader | text:greeneye | beside:greeneye\n"
                     "ultimate-asi-loader | Ultimate ASI Loader | text:IsUltimateASILoader | chain-as:xinput9_1_0.dll\n";

const char* kEmfMod = R"({"id":"clock","name":"Clock","version":"1.2.0","nexus_id":77})";

// An unpacked archive in a temp folder, and the plan for it against `game`.
struct Archive {
    TempGame dir;
    explicit Archive(const wchar_t* name) : dir(name, false) {}
    void add(const std::wstring& rel, const std::string& bytes = "x") { dir.write(rel, bytes); }
    PlanInput input(const TempGame& game, const std::string& archive = "Mod-1-0.zip") const {
        PlanInput in;
        in.game = game.dir;
        in.folder = dir.dir;
        uint64_t bytes = 0;
        std::string why;
        list_tree(dir.dir, in.files, bytes, why);
        in.archive = archive;
        in.tools_table = kTools;
        in.emf_installed = true;
        in.state.known = true;
        in.state.proxies = {{"dinput8.dll", "emf"}};
        return in;
    }
};

const PlanPart* part_of(const InstallPlan& p, const std::string& kind) {
    for (const auto& x : p.parts)
        if (x.kind == kind) return &x;
    return nullptr;
}

bool has_copy(const PlanPart& p, const std::string& to) {
    return std::any_of(p.copies.begin(), p.copies.end(), [&](const CopyStep& c) { return c.to == to; });
}

std::string all_lines(const PlanPart& p) {
    std::string s;
    for (const auto& l : p.lines) s += l + "\n";
    return s;
}

void emf_game(const TempGame& g) {
    g.write(L"dinput8.dll", "EMF-LOADER-7d3c9f");
    g.write(L"emf\\emf_core.dll", std::string("emf_core 1.2.3\0", 15));
}

}  // namespace

TEST_CASE("app install: names for renamed DLLs and install keys") {
    CHECK(name_slug("Auto Loot Plus") == "auto-loot-plus");
    CHECK(name_slug("  XHL-Core (1.0 ready)!! ") == "xhl-core-1-0-ready");
    CHECK(name_slug(std::string(60, 'a')).size() == 40);
    CHECK(archive_slug("AutoLootPlus 1.1.8-170-1-1-8-1792790875.zip") == "autolootplus");
    CHECK(archive_slug("Creative Mode v2.7z") == "creative-mode-v2");
    CHECK(archive_slug("C:\\Downloads\\quick_deposit-195-1-0.rar") == "quick-deposit");
    CHECK(product_slug({"Creative Mode", "", ""}) == "creative-mode");
    CHECK(product_slug({"Microsoft\xC2\xAE Windows\xC2\xAE Operating System", "DirectInput 8", ""}) == "directinput-8");
    CHECK(product_slug({"", "", "dinput8.dll"}) == "");  // the real DLL's own name says nothing
    CHECK(product_slug({"", "", "AutoLoot.dll"}) == "autoloot");
    CHECK(valid_install_key("nexus-170"));
    CHECK(valid_install_key("clock"));
    CHECK_FALSE(valid_install_key("../x"));
    CHECK_FALSE(valid_install_key(".."));
    CHECK_FALSE(valid_install_key("Upper"));
    CHECK_FALSE(valid_install_key(""));
}

TEST_CASE("app install: a DLL's version strings are read from its bytes") {
    std::string bytes;
    REQUIRE(read_file(L"C:\\Windows\\System32\\kernel32.dll", bytes));
    const auto v = dll_version_strings(bytes);
    REQUIRE(v.size() == 3);
    CHECK(v[0].find("Microsoft") != std::string::npos);
    CHECK(v[2].find("kernel32") != std::string::npos);
    // the installer's rule: Microsoft's product name is skipped, the description is used
    CHECK(product_slug(v) == "windows-nt-base-api-client-dll");
    CHECK(dll_version_strings("MZ not a dll") == std::vector<std::string>(3));
    CHECK(dll_version_strings(std::string(100000, '\xFF')) == std::vector<std::string>(3));
}

TEST_CASE("app install: an EML mod goes to mods\\<id>\\, a ShroudForge mod too") {
    TempGame g(L"inst_eml");
    Archive a(L"inst_eml_a");
    a.add(L"mods\\foo\\mod.json", R"({"id":"foo","name":"Foo Mod","version":"1.0"})");
    a.add(L"mods\\foo\\src\\mod.lua", "-- lua");
    a.add(L"README.md", "read me");
    const InstallPlan p = build_plan(a.input(g));
    REQUIRE_MESSAGE(p.error.empty(), p.error);
    const PlanPart* part = part_of(p, "eml-mod");
    REQUIRE(part);
    CHECK(part->action == Action::Install);
    CHECK(part->target == "mods\\foo\\");
    CHECK(has_copy(*part, "mods\\foo\\mod.json"));
    CHECK(has_copy(*part, "mods\\foo\\src\\mod.lua"));
    CHECK(part->copies.size() == 2);
    CHECK(p.name == "Foo Mod");
    CHECK(p.key == "foo");
    CHECK(p.installable());
    CHECK(std::any_of(p.notes.begin(), p.notes.end(), [](const std::string& n) { return n.find("README.md") != std::string::npos; }));

    Archive sf(L"inst_sf_a");
    sf.add(L"Bar-1.2\\bar\\mod.json", R"({"id":"bar"})");
    sf.add(L"Bar-1.2\\bar\\src\\mod.lua");
    sf.add(L"Bar-1.2\\bar\\extended.mod.json", "{}");
    const InstallPlan q = build_plan(sf.input(g));
    const PlanPart* sp = part_of(q, "shroudforge-mod");
    REQUIRE(sp);
    CHECK(sp->target == "mods\\bar\\");
    CHECK(has_copy(*sp, "mods\\bar\\extended.mod.json"));
}

TEST_CASE("app install: an EMF mod goes to emf\\mods\\<id>\\ when its mod.json is one EMF reads") {
    TempGame g(L"inst_emf");
    Archive a(L"inst_emf_a");
    a.add(L"emf\\mods\\clock\\mod.json", kEmfMod);
    a.add(L"emf\\mods\\clock\\clock.dll", "dll");
    const InstallPlan p = build_plan(a.input(g));
    const PlanPart* part = part_of(p, "emf-mod");
    REQUIRE(part);
    CHECK(part->action == Action::Install);
    CHECK(part->target == "emf\\mods\\clock\\");
    CHECK(part->version == "1.2.0");
    CHECK(p.key == "nexus-77");  // nexus_id in its mod.json
    CHECK(p.nexus_id == "77");

    Archive bad(L"inst_emf_bad");
    bad.add(L"emf\\mods\\broken\\mod.json", R"({"name":"no id"})");
    bad.add(L"emf\\mods\\broken\\broken.dll");
    const InstallPlan q = build_plan(bad.input(g));
    const PlanPart* bp = part_of(q, "emf-mod");
    REQUIRE(bp);
    CHECK(bp->action == Action::Refused);
    CHECK(bp->why.find("not one EMF reads") != std::string::npos);
    CHECK_FALSE(q.installable());
}

TEST_CASE("app install: a dinput8.dll mod is renamed like the installer does and listed in dinput8-chain.ini") {
    TempGame g(L"inst_dll");
    emf_game(g);
    Archive a(L"inst_dll_a");
    a.add(L"CreativeMode\\dinput8.dll", "MZ creative-mode-marker");
    a.add(L"CreativeMode\\creative.ini", "[x]");
    a.add(L"CreativeMode\\README.txt", "read me");
    InstallPlan p = build_plan(a.input(g));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(part->action == Action::Install);
    CHECK(has_copy(*part, "creative.dll"));  // <tool>.dll from known_tools.txt
    CHECK(has_copy(*part, "creative.ini"));
    CHECK(has_copy(*part, "README.txt"));
    CHECK(part->chain_add == std::vector<std::string>{"creative.dll"});
    CHECK(all_lines(*part).find("installed as creative.dll") != std::string::npos);
    CHECK(p.key == "mod");  // "Mod-1-0.zip" -> "mod"

    // unknown tool, no version information: the archive's name
    Archive b(L"inst_dll_b");
    b.add(L"dinput8.dll", "MZ something");
    p = build_plan(b.input(g, "AutoLootPlus 1.1.8-170-1-1-8-1792790875.zip"));
    part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "autolootplus.dll"));

    // the name is taken by another file that is not listed: -2
    g.write(L"autolootplus.dll", "someone else's");
    p = build_plan(b.input(g, "AutoLootPlus 1.1.8-170-1-1-8-1792790875.zip"));
    part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "autolootplus-2.dll"));
    CHECK(part->chain_add == std::vector<std::string>{"autolootplus-2.dll"});

    // EMF is not installed: refused, nothing half-installed
    PlanInput in = a.input(g);
    in.emf_installed = false;
    p = build_plan(in);
    part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(part->action == Action::Refused);
    CHECK(part->why.find("Install EMF first") != std::string::npos);
    CHECK(part->copies.empty());
}

TEST_CASE("app install: an older copy of the same known tool is switched off; the same file is not copied again") {
    TempGame g(L"inst_dll_old");
    emf_game(g);
    g.write(L"creative.dll", "MZ creative-mode-marker OLD");
    g.write(L"dinput8-chain.ini", "[Chain]\r\ncreative.dll\r\n");
    Archive a(L"inst_dll_old_a");
    a.add(L"dinput8.dll", "MZ creative-mode-marker NEW");
    InstallPlan p = build_plan(a.input(g));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "creative-2.dll"));
    CHECK(part->chain_off == std::vector<std::string>{"creative.dll"});

    Archive same(L"inst_dll_same_a");
    same.add(L"dinput8.dll", "MZ creative-mode-marker OLD");
    p = build_plan(same.input(g));
    part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(part->copies.empty());
    CHECK(part->action == Action::Skip);  // the same file, listed already
    CHECK_FALSE(p.installable());
}

TEST_CASE("app install: other proxy names only where the name is free; a second copy of a loader is not installed") {
    TempGame g(L"inst_proxy");
    emf_game(g);
    Archive a(L"inst_proxy_a");
    a.add(L"powrprof.dll", "MZ a mod");
    a.add(L"INSTALL.txt", "how to");
    InstallPlan p = build_plan(a.input(g));
    const PlanPart* part = part_of(p, "proxy-dll");
    REQUIRE(part);
    CHECK(part->action == Action::Install);
    CHECK(has_copy(*part, "powrprof.dll"));

    g.write(L"powrprof.dll", "MZ another tool");
    PlanInput in = a.input(g);
    in.state.proxies.push_back({"powrprof.dll", "eml"});
    p = build_plan(in);
    part = part_of(p, "proxy-dll");
    REQUIRE(part);
    CHECK(part->action == Action::Refused);
    CHECK(part->why.find("powrprof.dll (EML (Enshrouded Mod Loader)'s)") != std::string::npos);
    CHECK(part->copies.empty());

    // ShroudForge's release: its dinput8.dll is a second copy of the loader, so it stays out
    Archive sf(L"inst_proxy_sf");
    sf.add(L"dbghelp.dll", "MZ shroudforge");
    sf.add(L"dinput8.dll", "MZ shroudforge");
    sf.add(L"shroudforge\\config\\a.json", "{}");
    p = build_plan(sf.input(g));
    part = part_of(p, "proxy-dll");
    REQUIRE(part);
    CHECK(has_copy(*part, "dbghelp.dll"));
    CHECK(has_copy(*part, "shroudforge\\config\\a.json"));
    CHECK_FALSE(has_copy(*part, "dinput8.dll"));
    CHECK(all_lines(*part).find("second copy") != std::string::npos);
}

TEST_CASE("app install: Greeneye mods, with the loader's dbgcore.dll only when none is there") {
    TempGame g(L"inst_green");
    Archive a(L"inst_green_a");
    a.add(L"greeneye\\speed\\mod.json", R"({"id":"speed","name":"Speed"})");
    a.add(L"greeneye\\speed\\speed.dll");
    a.add(L"dbgcore.dll", "MZ greeneye loader");
    InstallPlan p = build_plan(a.input(g));
    const PlanPart* mod = part_of(p, "greeneye-mod");
    REQUIRE(mod);
    CHECK(mod->target == "greeneye\\speed\\");
    const auto loader = std::find_if(p.parts.begin(), p.parts.end(), [](const PlanPart& x) { return x.label == "Greeneye Loader"; });
    REQUIRE(loader != p.parts.end());
    CHECK(has_copy(*loader, "dbgcore.dll"));

    g.write(L"dbgcore.dll", "MZ greeneye older");
    PlanInput in = a.input(g);
    in.state.proxies.push_back({"dbgcore.dll", "greeneye"});
    p = build_plan(in);
    const auto kept = std::find_if(p.parts.begin(), p.parts.end(), [](const PlanPart& x) { return x.label == "Greeneye Loader"; });
    REQUIRE(kept != p.parts.end());
    CHECK(kept->copies.empty());
    CHECK(all_lines(*kept).find("installed already") != std::string::npos);
    CHECK(part_of(p, "greeneye-mod")->installs());
}

TEST_CASE("app install: ReShade, saves, Cheat Engine tables, unknown files and EMF itself are explained, not installed") {
    TempGame g(L"inst_explain");
    const auto plan_for = [&](const std::vector<std::wstring>& files) {
        static int n = 0;
        Archive a((L"inst_explain_" + std::to_wstring(++n)).c_str());
        for (const auto& f : files) a.add(f);
        return build_plan(a.input(g));
    };
    InstallPlan p = plan_for({L"MyPreset-ReShade.ini"});
    REQUIRE(part_of(p, "reshade-preset"));
    CHECK(part_of(p, "reshade-preset")->action == Action::Skip);
    CHECK_FALSE(p.installable());
    p = plan_for({L"characters", L"3ad85aea", L"3ad85aea_info"});
    REQUIRE(part_of(p, "save-files"));
    CHECK(part_of(p, "save-files")->why.find("never touches saves") != std::string::npos);
    p = plan_for({L"Enshrouded.CT"});
    REQUIRE(part_of(p, "cheat-table"));
    p = plan_for({L"stuff\\thing.bin", L"other.dat"});
    REQUIRE(part_of(p, "unknown"));
    CHECK_FALSE(p.installable());
    p = plan_for({L"emf\\emf_core.dll", L"dinput8.dll"});
    CHECK(p.error.find("EMF itself") != std::string::npos);
    CHECK_FALSE(p.installable());
}

TEST_CASE("app install: an archive with several layouts installs each part it can (Auto Loot Plus)") {
    TempGame g(L"inst_multi");
    emf_game(g);
    Archive a(L"inst_multi_a");
    a.add(L"dinput8.dll", "MZ unknown dll");
    a.add(L"AutoLootPlus-README.txt", "readme");
    a.add(L"licenses\\AutoLootPlus-LICENSE.txt", "mit");
    a.add(L"mods\\auto-loot-plus\\mod.json", R"({"id":"auto-loot-plus","name":"Auto Loot Plus","version":"1.1.8"})");
    a.add(L"mods\\auto-loot-plus\\src\\mod.lua");
    PlanInput in = a.input(g, "AutoLootPlus 1.1.8-170-1-1-8-1792790875.zip");
    in.nexus_id = "170";
    const InstallPlan p = build_plan(in);
    REQUIRE_MESSAGE(p.error.empty(), p.error);
    CHECK(p.key == "nexus-170");
    const PlanPart* dll = part_of(p, "dll-dinput8");
    const PlanPart* eml = part_of(p, "eml-mod");
    REQUIRE(dll);
    REQUIRE(eml);
    CHECK(has_copy(*dll, "autolootplus.dll"));
    CHECK(has_copy(*dll, "licenses\\AutoLootPlus-LICENSE.txt"));
    CHECK(has_copy(*dll, "AutoLootPlus-README.txt"));
    CHECK_FALSE(has_copy(*dll, "mods\\auto-loot-plus\\mod.json"));
    CHECK(eml->target == "mods\\auto-loot-plus\\");
    CHECK(eml->copies.size() == 2);
}

TEST_CASE("app install: nothing is overwritten: another mod's file refuses the part, a read-me is skipped") {
    TempGame g(L"inst_conflict");
    emf_game(g);
    g.write(L"settings.cfg", "theirs");
    g.write(L"README.txt", "theirs");
    Archive a(L"inst_conflict_a");
    a.add(L"dinput8.dll", "MZ mine");
    a.add(L"README.txt", "mine");
    InstallPlan p = build_plan(a.input(g));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(part->action == Action::Install);
    CHECK_FALSE(has_copy(*part, "README.txt"));
    CHECK(all_lines(*part).find("README.txt") != std::string::npos);
    a.add(L"settings.cfg", "mine");
    p = build_plan(a.input(g));
    part = part_of(p, "dll-dinput8");
    CHECK(part->action == Action::Refused);
    CHECK(part->why.find("settings.cfg") != std::string::npos);
    CHECK(part->copies.empty());
}

TEST_CASE("app install: unsafe paths refuse the whole archive") {
    TempGame g(L"inst_unsafe");
    PlanInput in;
    in.game = g.dir;
    in.folder = g.dir;
    in.archive = "x.zip";
    in.files = {"ok.txt", "../evil.txt"};
    InstallPlan p = build_plan(in);
    CHECK(p.error.find("../evil.txt") != std::string::npos);
    CHECK_FALSE(p.installable());
    in.files = {"mods/con/mod.json"};
    CHECK_FALSE(build_plan(in).error.empty());
    InstallPlan forged;  // a plan whose step reaches outside is refused when applied
    forged.key = "x";
    PlanPart part;
    part.copies.push_back({g.at(L"enshrouded.exe"), "..\\outside.txt", 1});
    forged.parts.push_back(part);
    const InstallResult r = apply_plan(g.dir, forged);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(g.dir).parent_path() / "outside.txt"));
}

TEST_CASE("app install: applying copies the files, records them with SHA-256 and the scan links the mod to it") {
    TempGame g(L"inst_apply");
    Archive a(L"inst_apply_a");
    a.add(L"mods\\foo\\mod.json", R"({"id":"foo","name":"Foo Mod","version":"1.0","nexus_id":"42"})");
    a.add(L"mods\\foo\\src\\mod.lua", "-- v1");
    const InstallPlan p = build_plan(a.input(g));
    const InstallResult r = apply_plan(g.dir, p);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(g.read(L"mods\\foo\\src\\mod.lua") == "-- v1");
    const auto rec = read_install_record(g.dir, "nexus-42");
    REQUIRE(rec);
    CHECK(rec->name == "Foo Mod");
    CHECK(rec->nexus_id == "42");
    CHECK(rec->roots == std::vector<std::string>{"mods\\foo"});
    REQUIRE(rec->files.size() == 2);
    CHECK(rec->files[0].second == file_sha256(g.at(wide(rec->files[0].first))));
    CHECK(std::find(rec->folders.begin(), rec->folders.end(), "mods\\foo\\src") != rec->folders.end());
    const FolderState s = scan_game_folder(g.dir);
    REQUIRE(s.installed.size() == 1);
    CHECK(s.installed[0].key == "nexus-42");
    const auto m = std::find_if(s.mods.begin(), s.mods.end(), [](const ModEntry& e) { return e.id == "foo"; });
    REQUIRE(m != s.mods.end());
    CHECK(m->install_key == "nexus-42");
}

TEST_CASE("app install: an update keeps the mod's own data in place and backs up only what it replaces or drops") {
    TempGame g(L"inst_update");
    Archive v1(L"inst_update_v1");
    v1.add(L"mods\\foo\\mod.json", R"({"id":"foo","version":"1.0"})");
    v1.add(L"mods\\foo\\src\\mod.lua", "-- v1");
    v1.add(L"mods\\foo\\old_only.txt", "gone in v2");
    REQUIRE(apply_plan(g.dir, build_plan(v1.input(g))).ok);
    g.write(L"mods\\foo\\player_notes.txt", "mine");       // the player's own file in the folder
    g.write(L"mods\\foo\\data\\save.bin", "the mod's own data");  // emf.dir-style saved data

    Archive v2(L"inst_update_v2");
    v2.add(L"mods\\foo\\mod.json", R"({"id":"foo","version":"2.0"})");
    v2.add(L"mods\\foo\\src\\mod.lua", "-- v2");
    const InstallPlan p = build_plan(v2.input(g));
    CHECK(p.update);
    REQUIRE_FALSE(p.notes.empty());
    CHECK(p.notes[0].find("This replaces foo 1.0, which EMF installed on ") != std::string::npos);
    const PlanPart* part = part_of(p, "eml-mod");
    REQUIRE(part);
    CHECK(part->action == Action::Update);
    CHECK(part->replace_folder == "mods\\foo");
    const InstallResult r = apply_plan(g.dir, p);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(g.read(L"mods\\foo\\src\\mod.lua") == "-- v2");          // the new version
    CHECK(g.read(L"mods\\foo\\player_notes.txt") == "mine");       // the player's data carried over, in place
    CHECK(g.read(L"mods\\foo\\data\\save.bin") == "the mod's own data");
    CHECK_FALSE(g.has(L"mods\\foo\\old_only.txt"));               // v1 installed it, v2 drops it: moved to backup
    bool found_old = false;
    for (const auto& e : std::filesystem::directory_iterator(g.at(L"emf\\backup")))
        for (const auto& f : std::filesystem::recursive_directory_iterator(e.path()))
            if (f.path().filename() == L"old_only.txt") found_old = true;
    CHECK(found_old);
    const auto rec = read_install_record(g.dir, "foo");
    REQUIRE(rec);
    CHECK(rec->version == "2.0");
    CHECK_FALSE(rec->backups.empty());
    for (const auto& f : rec->folders) CHECK_MESSAGE(f.rfind("emf\\backup", 0) != 0, f);  // backups are not the install's
    REQUIRE(uninstall_mod(g.dir, "foo").ok);
    CHECK(g.has(L"emf\\backup"));  // uninstall leaves the earlier versions
}

TEST_CASE("app install: a DLL mod in a UTF-16 dinput8-chain.ini; a file that appears before Install undoes it all") {
    TempGame g(L"inst_apply_dll");
    emf_game(g);
    const std::wstring w = L"[Chain]\r\nother.dll\r\n\r\n[Settings]\r\ndelay=1\r\n";
    std::string utf16 = "\xFF\xFE";
    utf16.append(reinterpret_cast<const char*>(w.data()), w.size() * 2);
    g.write(L"dinput8-chain.ini", utf16);
    g.write(L"other.dll", "MZ other");
    Archive a(L"inst_apply_dll_a");
    a.add(L"dinput8.dll", "MZ creative-mode-marker");
    a.add(L"creative.ini", "[x]");
    const InstallPlan p = build_plan(a.input(g));
    REQUIRE(p.installable());

    g.write(L"creative.ini", "appeared after the plan");  // the player put a file there meanwhile
    InstallResult r = apply_plan(g.dir, p);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("creative.ini") != std::string::npos);
    CHECK_FALSE(g.has(L"creative.dll"));
    CHECK(g.read(L"creative.ini") == "appeared after the plan");
    CHECK(g.read(L"dinput8-chain.ini") == utf16);
    CHECK_FALSE(g.has(L"emf\\installed_mods\\mod.json"));

    std::filesystem::remove(g.at(L"creative.ini"));
    r = apply_plan(g.dir, p);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(g.read(L"creative.dll") == "MZ creative-mode-marker");
    const std::string after = g.read(L"dinput8-chain.ini");
    REQUIRE(after.compare(0, 2, "\xFF\xFE") == 0);  // still UTF-16
    const std::wstring text(reinterpret_cast<const wchar_t*>(after.data() + 2), (after.size() - 2) / 2);
    CHECK(text == L"[Chain]\r\nother.dll\r\ncreative.dll\r\n\r\n[Settings]\r\ndelay=1\r\n");
    const auto rec = read_install_record(g.dir, "mod");
    REQUIRE(rec);
    CHECK(rec->chain == std::vector<std::string>{"creative.dll"});
    const FolderState s = scan_game_folder(g.dir);
    const auto m = std::find_if(s.mods.begin(), s.mods.end(), [](const ModEntry& e) { return e.source == "chain" && e.id == "creative.dll"; });
    REQUIRE(m != s.mods.end());
    CHECK(m->install_key == "mod");
}

TEST_CASE("app install: uninstall removes exactly what was installed; changed files are left and named") {
    TempGame g(L"inst_uninstall");
    emf_game(g);
    g.write(L"creative.dll", "MZ creative-mode-marker OLD");
    g.write(L"dinput8-chain.ini", "[Chain]\ncreative.dll\n");
    Archive a(L"inst_uninstall_a");
    a.add(L"dinput8.dll", "MZ creative-mode-marker NEW");
    a.add(L"creative.ini", "[x]");
    a.add(L"docs\\guide.txt", "guide");
    a.add(L"mods\\cm\\mod.json", R"({"id":"cm"})");
    a.add(L"mods\\cm\\src\\mod.lua");
    const InstallPlan p = build_plan(a.input(g, "CreativeMode.zip"));
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(g.read(L"dinput8-chain.ini") == "[Chain]\n; creative.dll\ncreative-2.dll\n");
    g.write(L"creative.ini", "[x]\nchanged by the player");
    g.write(L"mods\\cm\\notes.txt", "the player's");

    const InstallResult r = uninstall_mod(g.dir, "cm");
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(g.has(L"creative-2.dll"));
    CHECK_FALSE(g.has(L"docs"));  // the folder it made, empty now
    CHECK_FALSE(g.has(L"mods\\cm\\src"));
    CHECK(g.has(L"mods\\cm\\notes.txt"));  // not the install's
    CHECK(g.read(L"creative.ini") == "[x]\nchanged by the player");
    CHECK(g.read(L"creative.dll") == "MZ creative-mode-marker OLD");
    CHECK(g.read(L"dinput8-chain.ini") == "[Chain]\ncreative.dll\n");  // its line out, the old copy on again
    std::string said;
    for (const auto& l : r.done) said += l + "\n";
    CHECK(said.find("creative.ini") != std::string::npos);
    CHECK_FALSE(read_install_record(g.dir, "cm"));
    CHECK_FALSE(uninstall_mod(g.dir, "cm").ok);  // nothing recorded any more
}

TEST_CASE("app install: a record that points outside the game folder is not followed") {
    TempGame g(L"inst_forged");
    TempGame outside(L"inst_forged_outside", false);
    outside.write(L"victim.txt", "abc");
    const std::string rel = "..\\" + utf8(std::filesystem::path(outside.dir).filename().wstring()) + "\\victim.txt";
    g.write(L"emf\\installed_mods\\evil.json",
            "{\"key\":\"evil\",\"folders\":[" + json_quote("..") + "],\"files\":[{\"path\":" + json_quote(rel) +
                ",\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"}]}");
    const auto rec = read_install_record(g.dir, "evil");
    REQUIRE(rec);
    CHECK(rec->files.empty());
    CHECK(rec->folders.empty());
    uninstall_mod(g.dir, "evil");
    CHECK(outside.read(L"victim.txt") == "abc");
}

TEST_CASE("app install: dinput8-chain.ini lines are added inside [Chain] and taken out again, as the installer does") {
    // no sections: at the end
    CHECK(*add_chain_line("a.dll\r\n", L"b.dll") == "a.dll\r\nb.dll\r\n");
    CHECK(*add_chain_line("a.dll", L"b.dll") == "a.dll\r\nb.dll\r\n");
    CHECK(*add_chain_line("", L"b.dll") == "b.dll\r\n");
    // in [Chain], before its blank lines and the next section; LF files keep LF
    CHECK(*add_chain_line("[Chain]\na.dll\n\n[Other]\nx=1\n", L"b.dll") == "[Chain]\na.dll\nb.dll\n\n[Other]\nx=1\n");
    CHECK(*add_chain_line("\xEF\xBB\xBF[Chain]\r\na.dll\r\n", L"b.dll") == "\xEF\xBB\xBF[Chain]\r\na.dll\r\nb.dll\r\n");
    // sections but no [Chain]: a new one at the end
    CHECK(*add_chain_line("[Other]\r\nx=1\r\n", L"b.dll") == "[Other]\r\nx=1\r\n\r\n[Chain]\r\nb.dll\r\n");
    // listed already (case ignored), or switched off: on again
    CHECK_FALSE(add_chain_line("[Chain]\r\nB.DLL\r\n", L"b.dll"));
    CHECK(*add_chain_line("[Chain]\r\n; b.dll\r\n", L"b.dll") == "[Chain]\r\nb.dll\r\n");
    // a name only in another section does not count
    CHECK(*add_chain_line("[Chain]\r\n\r\n[Other]\r\nb.dll\r\n", L"b.dll") == "[Chain]\r\nb.dll\r\n\r\n[Other]\r\nb.dll\r\n");

    CHECK(*remove_chain_line("[Chain]\r\na.dll\r\nb.dll\r\n; b.dll\r\nc.dll\r\n", L"b.dll") == "[Chain]\r\na.dll\r\nc.dll\r\n");
    CHECK(*remove_chain_line("\xEF\xBB\xBF" "b.dll\nc.dll", L"b.dll") == "\xEF\xBB\xBF" "c.dll");
    CHECK_FALSE(remove_chain_line("[Chain]\r\na.dll\r\n[Other]\r\nb.dll\r\n", L"b.dll"));

    // a new file names its section
    TempGame g(L"inst_chain_new");
    emf_game(g);
    Archive a(L"inst_chain_new_a");
    a.add(L"dinput8.dll", "MZ creative-mode-marker");
    REQUIRE(apply_plan(g.dir, build_plan(a.input(g))).ok);
    CHECK(g.read(L"dinput8-chain.ini") == "[Chain]\r\ncreative.dll\r\n");
}

// ---- edge cases ----

TEST_CASE("app install: an archive may not plant files in emf\\ or EMF's own files") {
    TempGame g(L"inst_c1");
    emf_game(g);
    g.write(L"enshrouded.exe", "the real game");
    Archive a(L"inst_c1_a");
    a.add(L"dinput8.dll", "MZ creative-mode-marker");
    a.add(L"emf\\installed_mods\\forged.json", R"({"key":"forged","files":[]})");
    a.add(L"emf\\installed.txt", "enshrouded.exe\r\n");
    a.add(L"enshrouded.exe", "a fake game exe");
    const InstallPlan p = build_plan(a.input(g, "Cool-55-1-0-1792790875.zip"));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(part->action == Action::Refused);
    CHECK(part->why.find("EMF's own folder or the game's own file") != std::string::npos);
    CHECK(part->copies.empty());
    // even a hand-forged plan is refused at apply
    InstallPlan forged;
    forged.key = "x";
    PlanPart hp;
    hp.kind = "dll-dinput8";
    hp.copies.push_back({a.dir.at(L"dinput8.dll"), "emf\\installed_mods\\evil.json", 1});
    forged.parts.push_back(hp);
    const InstallResult r = apply_plan(g.dir, forged);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(g.has(L"emf\\installed_mods\\evil.json"));
}

TEST_CASE("app install: a forged record does not bind to a mod row and uninstall never deletes game files") {
    TempGame g(L"inst_c1_forge");
    emf_game(g);
    g.write(L"enshrouded.exe", "the real game");
    g.write(L"emf\\installed_mods\\forged.json",
            "{\"key\":\"forged\",\"name\":\"Evil\",\"files\":[{\"path\":\"enshrouded.exe\",\"sha256\":\"" +
                file_sha256(g.at(L"enshrouded.exe")) + "\"},{\"path\":\"dinput8.dll\",\"sha256\":\"" +
                file_sha256(g.at(L"dinput8.dll")) + "\"}]}");
    const FolderState s = scan_game_folder(g.dir);
    for (const auto& m : s.mods) CHECK_MESSAGE(m.install_key != "forged", m.id);  // C1.3: no row binds to it
    const InstallResult r = uninstall_mod(g.dir, "forged");
    CHECK(r.ok);
    CHECK(g.has(L"enshrouded.exe"));  // C1.2: the game's own files are kept
    CHECK(g.has(L"dinput8.dll"));
    CHECK(g.read(L"enshrouded.exe") == "the real game");
}

TEST_CASE("app install: a failed update puts the old version back") {
    TempGame g(L"inst_i1");
    Archive v1(L"inst_i1_v1");
    v1.add(L"mods\\foo\\mod.json", R"({"id":"foo","version":"1.0"})");
    v1.add(L"mods\\foo\\src\\mod.lua", "-- v1");
    REQUIRE(apply_plan(g.dir, build_plan(v1.input(g))).ok);
    g.write(L"mods\\foo\\keep.txt", "my data");
    Archive v2(L"inst_i1_v2");
    v2.add(L"mods\\foo\\mod.json", R"({"id":"foo","version":"2.0"})");
    v2.add(L"mods\\foo\\src\\mod.lua", "-- v2");
    const InstallPlan p = build_plan(v2.input(g));
    // make writing the record impossible: a file where emf\installed_mods must be a folder
    std::filesystem::remove_all(g.at(L"emf\\installed_mods"));
    g.write(L"emf\\installed_mods", "not a folder");
    const InstallResult r = apply_plan(g.dir, p);
    CHECK_FALSE(r.ok);
    CHECK(g.read(L"mods\\foo\\mod.json").find("1.0") != std::string::npos);  // v1 back
    CHECK(g.read(L"mods\\foo\\src\\mod.lua") == "-- v1");
    CHECK(g.read(L"mods\\foo\\keep.txt") == "my data");
}

TEST_CASE("app install: two mods sharing a file; uninstall keeps a file another install still needs") {
    TempGame g(L"inst_i4");
    Archive a(L"inst_i4_a"), b(L"inst_i4_b");
    a.add(L"greeneye\\moda\\mod.json", R"({"id":"moda"})");
    a.add(L"greeneye\\moda\\a.dll", "MZ a");
    a.add(L"dbgcore.dll", "MZ greeneye loader");
    b.add(L"greeneye\\modb\\mod.json", R"({"id":"modb"})");
    b.add(L"greeneye\\modb\\b.dll", "MZ b");
    b.add(L"dbgcore.dll", "MZ greeneye loader");
    REQUIRE(apply_plan(g.dir, build_plan(a.input(g, "moda.zip"))).ok);
    REQUIRE(apply_plan(g.dir, build_plan(b.input(g, "modb.zip"))).ok);
    CHECK(g.has(L"dbgcore.dll"));
    const auto rb = read_install_record(g.dir, "modb");
    REQUIRE(rb);
    CHECK(std::find(rb->deps.begin(), rb->deps.end(), "dbgcore.dll") != rb->deps.end());  // B records it as a dep
    const InstallResult ra = uninstall_mod(g.dir, "moda");
    REQUIRE(ra.ok);
    CHECK(g.has(L"dbgcore.dll"));  // B still needs it
    std::string said;
    for (const auto& l : ra.done) said += l + "\n";
    CHECK(said.find("also used by another mod") != std::string::npos);
}

TEST_CASE("app install: a shared loader goes with the last install that needs it, in either order; one that was there first stays") {
    const auto make = [](Archive& x, const wchar_t* id, const char* json) {
        x.add(std::wstring(L"greeneye\\") + id + L"\\mod.json", json);
        x.add(std::wstring(L"greeneye\\") + id + L"\\" + id + L".dll", "MZ mod");
        x.add(L"dbgcore.dll", "MZ greeneye loader");
    };
    for (const bool first_installer_first : {true, false}) {
        CAPTURE(first_installer_first);
        TempGame g(first_installer_first ? L"inst_share_ab" : L"inst_share_ba");
        Archive a(L"inst_share_a"), b(L"inst_share_b");
        make(a, L"moda", R"({"id":"moda"})");
        make(b, L"modb", R"({"id":"modb"})");
        REQUIRE(apply_plan(g.dir, build_plan(a.input(g, "moda.zip"))).ok);  // A installs the loader
        REQUIRE(apply_plan(g.dir, build_plan(b.input(g, "modb.zip"))).ok);  // B finds it there: a dep
        REQUIRE(uninstall_mod(g.dir, first_installer_first ? "moda" : "modb").ok);
        CHECK(g.has(L"dbgcore.dll"));  // the other one still needs it
        REQUIRE(uninstall_mod(g.dir, first_installer_first ? "modb" : "moda").ok);
        CHECK_FALSE(g.has(L"dbgcore.dll"));  // nobody needs it now, and EMF put it there
        CHECK(read_install_records(g.dir).empty());
    }
    // the player's own loader, there before either install: never removed
    TempGame g(L"inst_share_own");
    g.write(L"dbgcore.dll", "MZ greeneye loader");
    Archive a(L"inst_share_own_a"), b(L"inst_share_own_b");
    make(a, L"moda", R"({"id":"moda"})");
    make(b, L"modb", R"({"id":"modb"})");
    REQUIRE(apply_plan(g.dir, build_plan(a.input(g, "moda.zip"))).ok);
    REQUIRE(apply_plan(g.dir, build_plan(b.input(g, "modb.zip"))).ok);
    REQUIRE(uninstall_mod(g.dir, "moda").ok);
    REQUIRE(uninstall_mod(g.dir, "modb").ok);
    CHECK(g.has(L"dbgcore.dll"));
}

TEST_CASE("app install: a foreign dinput8.dll is never renamed to a proxy or system DLL name") {
    TempGame g(L"inst_i5");
    emf_game(g);
    Archive dxgi(L"inst_i5_dxgi");
    dxgi.add(L"dinput8.dll", "MZ unknown mod");
    InstallPlan p = build_plan(dxgi.input(g, "dxgi.zip"));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "dxgi-mod.dll"));  // not dxgi.dll (a system DLL / proxy name)
    CHECK_FALSE(has_copy(*part, "dxgi.dll"));
    Archive winmm(L"inst_i5_winmm");
    winmm.add(L"dinput8.dll", "MZ unknown mod 2");
    p = build_plan(winmm.input(g, "winmm-1-0.zip"));
    part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "winmm-mod.dll"));
}

TEST_CASE("app install: Ultimate ASI Loader as dinput8.dll is installed under the name it runs as (chain-as)") {
    // it shows "This library isn't supported." and holds the game's start under any name but its proxy names (live,
    // 10.0.2)
    TempGame g(L"inst_ual");
    emf_game(g);
    Archive ual(L"inst_ual_zip");
    ual.add(L"dinput8.dll", std::string("MZ ultimate asi loader IsUltimateASILoader\0 exports", 51));
    const InstallPlan p = build_plan(ual.input(g, "Ultimate-ASI-Loader_x64.zip"));
    const PlanPart* part = part_of(p, "dll-dinput8");
    REQUIRE(part);
    CHECK(has_copy(*part, "xinput9_1_0.dll"));  // a name it accepts, which the game itself never loads
    CHECK_FALSE(has_copy(*part, "ultimate-asi-loader.dll"));
    CHECK_FALSE(has_copy(*part, "xinput9_1_0-mod.dll"));
}

TEST_CASE("app install: the Nexus id comes from the file name's mod id, not the version digits") {
    CHECK(filename_nexus_id_of("AutoLootPlus 1.1.8-170-1-1-8-1792790875.zip") == "170");
    CHECK(filename_nexus_id_of("Creative Mode-55-2-7-1790000000.7z") == "55");
    CHECK(filename_nexus_id_of("2-Handed-Mod-1-0.zip") == "");  // no timestamp: no id guessed
    CHECK(filename_nexus_id_of("quick_deposit.zip") == "");
}

// ---- several separate mods in one archive (the EMF creature packs) ----
namespace {

// The EMF creature packs download (scripts/package_packs.ps1): mods\<pack>\ folders and a README.txt, in a top folder.
void add_packs(Archive& a, const char* version = "1.0") {
    for (const char* pack : {"emf_beastmaster_bear", "emf_beastmaster_beasts", "emf_necromancer_undead"}) {
        const std::wstring dir = L"EMF-creature-packs-0.3.2\\mods\\" + wide(pack) + L"\\";
        a.add(dir + L"mod.json", std::string(R"({"id":")") + pack + R"(","name":")" + pack + R"( pack","version":")" + version + "\"}");
        a.add(dir + L"src\\mod.lua", std::string("-- ") + pack + " " + version);
        a.add(dir + L"models\\creature.bin", std::string("model ") + version);
    }
    a.add(L"EMF-creature-packs-0.3.2\\README.txt", "what is in it");
}

bool notes_say(const InstallPlan& p, const std::string& text) {
    return std::any_of(p.notes.begin(), p.notes.end(), [&](const std::string& n) { return n.find(text) != std::string::npos; });
}

bool has_root(const InstallRecord& r, const std::string& root) {
    return std::find(r.roots.begin(), r.roots.end(), root) != r.roots.end();
}

std::string row_key(const FolderState& s, const std::string& id) {
    for (const auto& m : s.mods)
        if (m.id == id) return m.install_key;
    return "(no row)";
}

}  // namespace

TEST_CASE("app install: several mods in one archive are recorded one by one, and each uninstalls on its own (the creature packs)") {
    TempGame g(L"inst_packs");
    Archive a(L"inst_packs_a");
    add_packs(a);
    const InstallPlan p = build_plan(a.input(g, "EMF-creature-packs-0.3.2.zip"));
    REQUIRE_MESSAGE(p.error.empty(), p.error);
    REQUIRE(p.parts.size() == 3);
    for (const auto& part : p.parts) {
        CHECK(part.action == Action::Install);
        CHECK(part.key == part.id);  // an install of its own, named by its folder
    }
    CHECK(notes_say(p, "recorded on its own"));
    CHECK(notes_say(p, "README.txt"));  // not part of any mod: not copied, and the sheet says so

    const InstallResult r = apply_plan(g.dir, p);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.keys == std::vector<std::string>{"emf_beastmaster_bear", "emf_beastmaster_beasts", "emf_necromancer_undead"});
    CHECK_FALSE(g.has(L"README.txt"));
    CHECK_FALSE(g.has(L"mods\\README.txt"));
    const auto records = read_install_records(g.dir);
    REQUIRE(records.size() == 3);
    for (const auto& rec : records) {
        CAPTURE(rec.key);
        CHECK(rec.roots == std::vector<std::string>{"mods\\" + rec.key});
        CHECK(rec.name == rec.key + " pack");
        CHECK(rec.archive == "EMF-creature-packs-0.3.2.zip");
        CHECK(rec.files.size() == 3);
        for (const auto& [path, hash] : rec.files) CHECK(path.rfind("mods\\" + rec.key + "\\", 0) == 0);
    }
    const FolderState s = scan_game_folder(g.dir);
    CHECK(row_key(s, "emf_beastmaster_bear") == "emf_beastmaster_bear");
    CHECK(row_key(s, "emf_necromancer_undead") == "emf_necromancer_undead");

    // uninstalling the bear pack leaves the other packs and their records alone
    const InstallResult u = uninstall_mod(g.dir, "emf_beastmaster_bear");
    REQUIRE_MESSAGE(u.ok, u.error);
    CHECK_FALSE(g.has(L"mods\\emf_beastmaster_bear"));
    CHECK(g.read(L"mods\\emf_necromancer_undead\\src\\mod.lua") == "-- emf_necromancer_undead 1.0");
    CHECK(g.has(L"mods\\emf_beastmaster_beasts\\models\\creature.bin"));
    CHECK(read_install_records(g.dir).size() == 2);
    REQUIRE(uninstall_mod(g.dir, "emf_necromancer_undead").ok);
    REQUIRE(uninstall_mod(g.dir, "emf_beastmaster_beasts").ok);
    CHECK_FALSE(g.has(L"mods"));  // the install made it, and it is empty now
    CHECK(read_install_records(g.dir).empty());
}

TEST_CASE("app install: installing the packs again updates each pack's own install; one install of them all (EMF.exe 0.3.2) is split") {
    for (const bool from_nexus : {false, true}) {
        CAPTURE(from_nexus);
        TempGame g(from_nexus ? L"inst_packs_up_nexus" : L"inst_packs_up");
        Archive v1(L"inst_packs_v1");
        add_packs(v1, "1.0");
        PlanInput in1 = v1.input(g, "EMF-creature-packs-0.3.2.zip");
        if (from_nexus) in1.nexus_id = "900";
        InstallPlan old = build_plan(in1);
        for (auto& part : old.parts) part.key.clear();  // as EMF.exe 0.3.2 did: one record for every pack
        REQUIRE(apply_plan(g.dir, old).ok);
        const std::string old_key = from_nexus ? "nexus-900" : "emf_beastmaster_bear";
        REQUIRE(read_install_records(g.dir).size() == 1);
        REQUIRE(read_install_record(g.dir, old_key)->roots.size() == 3);
        g.write(L"mods\\emf_necromancer_undead\\player.txt", "the player's own file");

        Archive v2(L"inst_packs_v2");
        add_packs(v2, "2.0");
        PlanInput in2 = v2.input(g, "EMF-creature-packs-0.3.3.zip");
        if (from_nexus) in2.nexus_id = "900";
        const InstallPlan p = build_plan(in2);
        CHECK(p.update);
        CHECK(notes_say(p, "This replaces"));
        for (const auto& part : p.parts) {
            CHECK(part.action == Action::Update);
            CHECK(part.key == part.id);
        }
        const InstallResult r = apply_plan(g.dir, p);
        REQUIRE_MESSAGE(r.ok, r.error);
        const auto records = read_install_records(g.dir);
        REQUIRE(records.size() == 3);  // the one record of them all is gone
        for (const auto& rec : records) {
            CAPTURE(rec.key);
            CHECK(rec.roots == std::vector<std::string>{"mods\\" + rec.key});
            CHECK(rec.version == "2.0");
            CHECK(rec.nexus_id == (from_nexus ? "900" : ""));
            for (const auto& [path, hash] : rec.files) CHECK(path.rfind("mods\\" + rec.key + "\\", 0) == 0);
        }
        CHECK(g.read(L"mods\\emf_beastmaster_bear\\src\\mod.lua") == "-- emf_beastmaster_bear 2.0");
        CHECK(g.read(L"mods\\emf_necromancer_undead\\player.txt") == "the player's own file");  // kept in place
        CHECK(g.has(L"emf\\backup"));  // the old files, never deleted

        // and again: each pack's install is updated under its own key
        const InstallPlan again = build_plan(v2.input(g, "EMF-creature-packs-0.3.3.zip"));
        REQUIRE(apply_plan(g.dir, again).ok);
        CHECK(read_install_records(g.dir).size() == 3);
        REQUIRE(uninstall_mod(g.dir, "emf_beastmaster_beasts").ok);
        CHECK(g.has(L"mods\\emf_beastmaster_bear\\mod.json"));
        CHECK_FALSE(g.has(L"mods\\emf_beastmaster_beasts"));
    }
}

TEST_CASE("app install: a pack whose folder name another install's key already has gets a key of its own") {
    TempGame g(L"inst_packs_taken");
    Archive other(L"inst_packs_taken_other");  // an unrelated Greeneye mod with the same folder name
    other.add(L"greeneye\\emf_beastmaster_bear\\mod.json", R"({"id":"emf_beastmaster_bear"})");
    other.add(L"greeneye\\emf_beastmaster_bear\\bear.dll", "MZ unrelated");
    REQUIRE(apply_plan(g.dir, build_plan(other.input(g, "other.zip"))).ok);
    REQUIRE(read_install_record(g.dir, "emf_beastmaster_bear"));
    Archive a(L"inst_packs_taken_a");
    add_packs(a);
    const InstallPlan p = build_plan(a.input(g, "EMF-creature-packs-0.3.2.zip"));
    CHECK_FALSE(p.update);  // the unrelated install is no earlier version of the pack
    const auto bear = std::find_if(p.parts.begin(), p.parts.end(), [](const PlanPart& x) { return x.id == "emf_beastmaster_bear"; });
    REQUIRE(bear != p.parts.end());
    CHECK(bear->key == "emf_beastmaster_bear-2");
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(g.read(L"greeneye\\emf_beastmaster_bear\\bear.dll") == "MZ unrelated");  // left alone
    CHECK(read_install_record(g.dir, "emf_beastmaster_bear")->roots == std::vector<std::string>{"greeneye\\emf_beastmaster_bear"});
    CHECK(read_install_record(g.dir, "emf_beastmaster_bear-2")->roots == std::vector<std::string>{"mods\\emf_beastmaster_bear"});
}

TEST_CASE("app install: mods that install files outside their folders stay one install, and the plan says they go together") {
    TempGame g(L"inst_together");
    emf_game(g);
    Archive a(L"inst_together_a");  // Auto Loot Plus: a DLL mod next to the game and an EML mod
    a.add(L"dinput8.dll", "MZ unknown dll");
    a.add(L"mods\\auto-loot-plus\\mod.json", R"({"id":"auto-loot-plus","name":"Auto Loot Plus","version":"1.1.8"})");
    a.add(L"mods\\auto-loot-plus\\src\\mod.lua");
    const InstallPlan p = build_plan(a.input(g, "AutoLootPlus.zip"));
    for (const auto& part : p.parts) CHECK(part.key.empty());
    CHECK(notes_say(p, "one install"));
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(read_install_records(g.dir).size() == 1);

    Archive gr(L"inst_together_gr");  // two Greeneye mods with the Greeneye Loader: they need it
    gr.add(L"greeneye\\moda\\mod.json", R"({"id":"moda"})");
    gr.add(L"greeneye\\moda\\a.dll", "MZ a");
    gr.add(L"greeneye\\modb\\mod.json", R"({"id":"modb"})");
    gr.add(L"greeneye\\modb\\b.dll", "MZ b");
    gr.add(L"dbgcore.dll", "MZ greeneye loader");
    const InstallPlan q = build_plan(gr.input(g, "greeneye-pair.zip"));
    for (const auto& part : q.parts) CHECK(part.key.empty());
    CHECK(notes_say(q, "one install"));

    TempGame h(L"inst_split_greeneye");  // the loader already there (a dependency, not a file it installs): one by one
    h.write(L"dbgcore.dll", "MZ greeneye loader");
    PlanInput in = gr.input(h, "greeneye-pair.zip");
    in.state.proxies.push_back({"dbgcore.dll", "greeneye"});
    const InstallPlan s = build_plan(in);
    REQUIRE(apply_plan(h.dir, s).ok);
    const auto ra = read_install_record(h.dir, "moda"), rb = read_install_record(h.dir, "modb");
    REQUIRE(ra);
    REQUIRE(rb);
    CHECK(std::find(ra->deps.begin(), ra->deps.end(), "dbgcore.dll") != ra->deps.end());
    CHECK(std::find(rb->deps.begin(), rb->deps.end(), "dbgcore.dll") != rb->deps.end());
    REQUIRE(uninstall_mod(h.dir, "moda").ok);
    CHECK(h.has(L"greeneye\\modb\\b.dll"));
    CHECK(h.has(L"dbgcore.dll"));
}

TEST_CASE("app install: one record of several separate mods (EMF.exe 0.3.2's creature packs) uninstalls one mod's folder alone") {
    TempGame g(L"inst_packs_legacy");
    Archive a(L"inst_packs_legacy_a");
    add_packs(a);
    InstallPlan old = build_plan(a.input(g, "EMF-creature-packs-0.3.2.zip"));
    for (auto& part : old.parts) part.key.clear();  // as EMF.exe 0.3.2 did: one record for every pack
    REQUIRE(apply_plan(g.dir, old).ok);
    const std::string key = "emf_beastmaster_bear";
    const auto rec = read_install_record(g.dir, key);
    REQUIRE(rec);
    REQUIRE(rec->roots.size() == 3);
    CHECK(separable_install(*rec));
    const FolderState s = scan_game_folder(g.dir);
    for (const auto& m : s.mods)  // each row binds through its own folder: the page offers to uninstall that one
        if (m.source == "eml") CHECK(m.install_root == "mods\\" + m.id);
    REQUIRE(s.installed.size() == 1);
    CHECK(s.installed[0].separable);
    CHECK(s.installed[0].root_files == std::vector<size_t>{3, 3, 3});

    g.write(L"mods\\emf_beastmaster_bear\\player.txt", "mine");
    const InstallResult u = uninstall_mod(g.dir, key, "mods\\emf_beastmaster_bear");
    REQUIRE_MESSAGE(u.ok, u.error);
    CHECK_FALSE(g.has(L"mods\\emf_beastmaster_bear\\src"));
    CHECK_FALSE(g.has(L"mods\\emf_beastmaster_bear\\models"));
    CHECK(g.read(L"mods\\emf_beastmaster_bear\\player.txt") == "mine");  // not the install's
    CHECK(g.has(L"mods\\emf_necromancer_undead\\src\\mod.lua"));
    CHECK(g.has(L"mods\\emf_beastmaster_beasts\\models\\creature.bin"));
    const auto rest = read_install_record(g.dir, key);
    REQUIRE(rest);  // the record keeps the other two packs
    CHECK(rest->roots.size() == 2);
    CHECK_FALSE(has_root(*rest, "mods\\emf_beastmaster_bear"));
    CHECK(rest->files.size() == 6);
    for (const auto& [path, hash] : rest->files) CHECK(path.rfind("mods\\emf_beastmaster_bear\\", 0) != 0);

    CHECK_FALSE(uninstall_mod(g.dir, key, "mods\\emf_beastmaster_bear").ok);  // no longer the record's
    CHECK_FALSE(uninstall_mod(g.dir, key, "mods\\somewhere_else").ok);
    REQUIRE(uninstall_mod(g.dir, key, "mods\\emf_necromancer_undead").ok);
    CHECK_FALSE(g.has(L"mods\\emf_necromancer_undead"));
    REQUIRE(uninstall_mod(g.dir, key, "mods\\emf_beastmaster_beasts").ok);  // the last folder: the whole record goes
    CHECK_FALSE(g.has(L"mods\\emf_beastmaster_beasts"));
    CHECK_FALSE(read_install_record(g.dir, key));

    // Auto Loot Plus's DLL and EML folder go together: one folder alone is refused and nothing changes
    emf_game(g);
    Archive alp(L"inst_packs_legacy_alp");
    alp.add(L"dinput8.dll", "MZ unknown dll");
    alp.add(L"mods\\auto-loot-plus\\mod.json", R"({"id":"auto-loot-plus","name":"Auto Loot Plus","version":"1.1.8"})");
    alp.add(L"mods\\auto-loot-plus\\src\\mod.lua");
    const InstallPlan p = build_plan(alp.input(g, "AutoLootPlus.zip"));
    REQUIRE(apply_plan(g.dir, p).ok);
    const auto both = read_install_record(g.dir, p.key);
    REQUIRE(both);
    CHECK_FALSE(separable_install(*both));
    const InstallResult r = uninstall_mod(g.dir, p.key, "mods\\auto-loot-plus");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("together") != std::string::npos);
    CHECK(g.has(L"mods\\auto-loot-plus\\src\\mod.lua"));
    CHECK(g.has(L"autolootplus.dll"));
}

// ---- one record per mod folder: keys that clash, records from EMF.exe 0.3.2, smaller archives ----
namespace {

// How many install records hold `root`: one at most, or uninstall keeps its files as "also used" for ever.
size_t owners(const std::wstring& game, const std::string& root) {
    size_t n = 0;
    for (const auto& r : read_install_records(game)) n += has_root(r, root) ? 1 : 0;
    return n;
}

void one_owner_each(const std::wstring& game) {
    for (const char* pack : {"emf_beastmaster_bear", "emf_beastmaster_beasts", "emf_necromancer_undead"}) {
        CAPTURE(pack);
        CHECK(owners(game, std::string("mods\\") + pack) == 1);
    }
}

// One record for all three packs, as EMF.exe 0.3.2 wrote it.
void legacy_packs(const TempGame& g, const std::string& nexus_id = "") {
    Archive v1(L"inst_legacy_v1");
    add_packs(v1, "1.0");
    PlanInput in = v1.input(g, "EMF-creature-packs-0.3.2.zip");
    in.nexus_id = nexus_id;
    InstallPlan old = build_plan(in);
    for (auto& part : old.parts) part.key.clear();
    REQUIRE(apply_plan(g.dir, old).ok);
    REQUIRE(read_install_records(g.dir).size() == 1);
}

std::string done_text(const InstallResult& r) {
    std::string s;
    for (const auto& l : r.done) s += l + "\n";
    return s;
}

}  // namespace

TEST_CASE("app install: a pack keeps the record that holds its folder, also once the install that took its name is gone") {
    TempGame g(L"inst_keep_key");
    Archive other(L"inst_keep_key_other");  // an unrelated Greeneye mod with the bear pack's folder name
    other.add(L"greeneye\\emf_beastmaster_bear\\mod.json", R"({"id":"emf_beastmaster_bear"})");
    other.add(L"greeneye\\emf_beastmaster_bear\\bear.dll", "MZ unrelated");
    REQUIRE(apply_plan(g.dir, build_plan(other.input(g, "other.zip"))).ok);
    Archive v1(L"inst_keep_key_v1");
    add_packs(v1, "1.0");
    REQUIRE(apply_plan(g.dir, build_plan(v1.input(g, "EMF-creature-packs-0.3.2.zip"))).ok);
    REQUIRE(read_install_record(g.dir, "emf_beastmaster_bear-2"));
    REQUIRE(uninstall_mod(g.dir, "emf_beastmaster_bear").ok);  // the unrelated mod goes; its key is free now

    Archive v2(L"inst_keep_key_v2");
    add_packs(v2, "2.0");
    const InstallPlan p = build_plan(v2.input(g, "EMF-creature-packs-0.3.3.zip"));
    const auto bear = std::find_if(p.parts.begin(), p.parts.end(), [](const PlanPart& x) { return x.id == "emf_beastmaster_bear"; });
    REQUIRE(bear != p.parts.end());
    CHECK(bear->key == "emf_beastmaster_bear-2");  // the record that holds the folder
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(read_install_records(g.dir).size() == 3);
    one_owner_each(g.dir);
    CHECK(read_install_record(g.dir, "emf_beastmaster_bear-2")->version == "2.0");
    REQUIRE(uninstall_mod(g.dir, "emf_beastmaster_bear-2").ok);  // and it uninstalls: no other record keeps its files
    CHECK_FALSE(g.has(L"mods\\emf_beastmaster_bear"));
}

TEST_CASE("app install: one record of the packs under nexus-<id> or a folder's key is replaced by any packs archive, one record per pack") {
    {  // EMF.exe 0.3.2 installed the Nexus download; the player installs a zip picked by hand (keys by folder)
        TempGame g(L"inst_legacy_nexus_hand");
        legacy_packs(g, "900");
        Archive v2(L"inst_legacy_nexus_hand_v2");
        add_packs(v2, "2.0");
        const InstallPlan p = build_plan(v2.input(g, "EMF-creature-packs-0.3.3.zip"));
        CHECK(p.update);
        REQUIRE(apply_plan(g.dir, p).ok);
        CHECK_FALSE(read_install_record(g.dir, "nexus-900"));
        CHECK(read_install_records(g.dir).size() == 3);
        one_owner_each(g.dir);
    }
    {  // the packs recorded one by one; then a Nexus download of the bear pack alone (keyed nexus-<id>)
        TempGame g(L"inst_split_then_nexus");
        Archive v1(L"inst_split_then_nexus_v1");
        add_packs(v1, "1.0");
        REQUIRE(apply_plan(g.dir, build_plan(v1.input(g, "EMF-creature-packs-0.3.2.zip"))).ok);
        Archive one(L"inst_split_then_nexus_one");
        one.add(L"mods\\emf_beastmaster_bear\\mod.json", R"({"id":"emf_beastmaster_bear","name":"Bear pack","version":"2.0"})");
        one.add(L"mods\\emf_beastmaster_bear\\src\\mod.lua", "-- bear 2.0");
        PlanInput in = one.input(g, "Bear pack-900-2-0-1793520000.zip");
        in.nexus_id = "900";
        const InstallPlan p = build_plan(in);
        CHECK(p.update);
        REQUIRE(apply_plan(g.dir, p).ok);
        CHECK(read_install_records(g.dir).size() == 3);
        one_owner_each(g.dir);
        CHECK(g.read(L"mods\\emf_beastmaster_bear\\src\\mod.lua") == "-- bear 2.0");
    }
}

TEST_CASE("app install: a smaller archive replaces only its own folders of one record of the packs; the other packs keep a record") {
    TempGame g(L"inst_legacy_smaller");
    legacy_packs(g);  // one record "emf_beastmaster_bear" for all three packs
    Archive one(L"inst_legacy_smaller_one");
    one.add(L"mods\\emf_beastmaster_bear\\mod.json", R"({"id":"emf_beastmaster_bear","name":"Bear pack","version":"2.0"})");
    one.add(L"mods\\emf_beastmaster_bear\\src\\mod.lua", "-- bear 2.0");
    const InstallPlan p = build_plan(one.input(g, "bear-pack.zip"));
    CHECK(p.key == "emf_beastmaster_bear");
    CHECK(p.update);
    CHECK(notes_say(p, "stay installed"));  // the plan says the other packs stay
    const InstallResult r = apply_plan(g.dir, p);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(g.read(L"mods\\emf_beastmaster_bear\\src\\mod.lua") == "-- bear 2.0");
    CHECK(g.read(L"mods\\emf_beastmaster_beasts\\src\\mod.lua") == "-- emf_beastmaster_beasts 1.0");  // never moved
    CHECK(g.read(L"mods\\emf_necromancer_undead\\models\\creature.bin") == "model 1.0");
    const auto records = read_install_records(g.dir);
    REQUIRE(records.size() == 2);
    one_owner_each(g.dir);
    const auto bear = read_install_record(g.dir, "emf_beastmaster_bear");
    REQUIRE(bear);
    CHECK(bear->roots == std::vector<std::string>{"mods\\emf_beastmaster_bear"});
    CHECK(bear->version == "2.0");
    const auto rest = std::find_if(records.begin(), records.end(), [](const InstallRecord& x) { return x.key != "emf_beastmaster_bear"; });
    REQUIRE(rest != records.end());
    CHECK(rest->roots.size() == 2);
    CHECK(rest->files.size() == 6);
    CHECK(separable_install(*rest));
    REQUIRE(uninstall_mod(g.dir, rest->key, "mods\\emf_necromancer_undead").ok);  // each still uninstalls on its own
    CHECK_FALSE(g.has(L"mods\\emf_necromancer_undead"));
    CHECK(g.has(L"mods\\emf_beastmaster_beasts\\mod.json"));
}

TEST_CASE("app install: another install's record that holds a folder and a DLL keeps the DLL when an archive of the folder alone comes") {
    TempGame g(L"inst_trim_dll");
    emf_game(g);
    Archive both(L"inst_trim_dll_both");  // Auto Loot Plus, installed by hand: its DLL and its EML folder, one install
    both.add(L"dinput8.dll", "MZ unknown dll");
    both.add(L"mods\\auto-loot-plus\\mod.json", R"({"id":"auto-loot-plus","name":"Auto Loot Plus","version":"1.1.8"})");
    both.add(L"mods\\auto-loot-plus\\src\\mod.lua", "-- 1.1.8");
    const InstallPlan first = build_plan(both.input(g, "AutoLootPlus.zip"));
    REQUIRE(apply_plan(g.dir, first).ok);
    const std::string chain = g.read(L"dinput8-chain.ini");
    Archive eml(L"inst_trim_dll_eml");  // then the EML part alone, from its Nexus page: an install of its own
    eml.add(L"mods\\auto-loot-plus\\mod.json", R"({"id":"auto-loot-plus","name":"Auto Loot Plus","version":"1.2.0"})");
    eml.add(L"mods\\auto-loot-plus\\src\\mod.lua", "-- 1.2.0");
    PlanInput in = eml.input(g, "Auto Loot Plus EML-170-1-2-0-1793520000.zip");
    in.nexus_id = "170";
    const InstallPlan p = build_plan(in);
    CHECK(p.key == "nexus-170");
    CHECK(notes_say(p, "The rest of it stays installed"));
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(g.read(L"mods\\auto-loot-plus\\src\\mod.lua") == "-- 1.2.0");
    CHECK(g.has(L"autolootplus.dll"));                  // the DLL stays, with its line
    CHECK(g.read(L"dinput8-chain.ini") == chain);
    CHECK(owners(g.dir, "mods\\auto-loot-plus") == 1);  // the folder is the new install's alone
    const auto old = read_install_record(g.dir, first.key);
    REQUIRE(old);
    CHECK(old->roots.empty());
    CHECK(old->chain == std::vector<std::string>{"autolootplus.dll"});
    REQUIRE(uninstall_mod(g.dir, "nexus-170").ok);  // and each uninstalls what is its own
    CHECK_FALSE(g.has(L"mods\\auto-loot-plus"));
    CHECK(g.has(L"autolootplus.dll"));
    REQUIRE(uninstall_mod(g.dir, first.key).ok);
    CHECK_FALSE(g.has(L"autolootplus.dll"));
}

TEST_CASE("app install: an install with the same key but other mod folders is not this one's earlier version") {
    TempGame g(L"inst_key_clash");
    Archive a(L"inst_key_clash_a"), b(L"inst_key_clash_b");  // two files of one Nexus mod, each with its own folder
    a.add(L"mods\\moda\\mod.json", R"({"id":"moda","name":"Mod A","version":"1.0","nexus_id":170})");
    a.add(L"mods\\moda\\src\\mod.lua", "-- a");
    b.add(L"mods\\modb\\mod.json", R"({"id":"modb","name":"Mod B","version":"1.0","nexus_id":170})");
    b.add(L"mods\\modb\\src\\mod.lua", "-- b");
    REQUIRE(apply_plan(g.dir, build_plan(a.input(g, "a.zip"))).ok);
    REQUIRE(read_install_record(g.dir, "nexus-170"));
    const InstallPlan p = build_plan(b.input(g, "b.zip"));
    CHECK(p.key == "nexus-170-2");
    CHECK_FALSE(p.update);
    CHECK(notes_say(p, "recorded as an install of its own"));
    REQUIRE(apply_plan(g.dir, p).ok);
    CHECK(g.read(L"mods\\moda\\src\\mod.lua") == "-- a");  // Mod A stays
    CHECK(read_install_record(g.dir, "nexus-170")->roots == std::vector<std::string>{"mods\\moda"});
    CHECK(read_install_record(g.dir, "nexus-170-2")->roots == std::vector<std::string>{"mods\\modb"});
    REQUIRE(apply_plan(g.dir, build_plan(b.input(g, "b.zip"))).ok);  // B again: an update of its own install
    CHECK(read_install_records(g.dir).size() == 2);

    Archive gr(L"inst_key_clash_gr"), ml(L"inst_key_clash_ml");  // a folder's name as the key, in two loaders' folders
    gr.add(L"greeneye\\foo\\mod.json", R"({"id":"foo"})");
    gr.add(L"greeneye\\foo\\foo.dll", "MZ foo");
    ml.add(L"mods\\foo\\mod.json", R"({"id":"foo"})");
    ml.add(L"mods\\foo\\src\\mod.lua", "-- foo");
    REQUIRE(apply_plan(g.dir, build_plan(gr.input(g, "foo-greeneye.zip"))).ok);
    const InstallPlan q = build_plan(ml.input(g, "foo-eml.zip"));
    CHECK(q.key == "foo-2");
    REQUIRE(apply_plan(g.dir, q).ok);
    CHECK(g.read(L"greeneye\\foo\\foo.dll") == "MZ foo");
}

TEST_CASE("app install: each record of an archive of several mods lists its own backup; a pack's uninstall says its name") {
    TempGame g(L"inst_own_backup");
    Archive v1(L"inst_own_backup_v1"), v2(L"inst_own_backup_v2");
    add_packs(v1, "1.0");
    add_packs(v2, "2.0");
    REQUIRE(apply_plan(g.dir, build_plan(v1.input(g, "EMF-creature-packs-0.3.2.zip"))).ok);
    REQUIRE(apply_plan(g.dir, build_plan(v2.input(g, "EMF-creature-packs-0.3.3.zip"))).ok);
    for (const auto& rec : read_install_records(g.dir)) {
        CAPTURE(rec.key);
        REQUIRE(rec.backups.size() == 1);
        CHECK(rec.backups[0].find(rec.key) != std::string::npos);
        size_t files = 0;
        for (const auto& f : std::filesystem::recursive_directory_iterator(g.at(wide(rec.backups[0])))) {
            if (!f.is_regular_file()) continue;
            ++files;
            CHECK(f.path().wstring().find(wide(rec.key)) != std::wstring::npos);  // only this pack's old files
        }
        CHECK(files == 3);
    }

    TempGame h(L"inst_partial_name");
    legacy_packs(h);
    const InstallResult u = uninstall_mod(h.dir, "emf_beastmaster_bear", "mods\\emf_necromancer_undead");
    REQUIRE(u.ok);
    CHECK(done_text(u).find("Uninstalled emf_necromancer_undead pack: 3 files removed.") != std::string::npos);
}
