// The content loader's start sequence (src/loader/content_start.h), with recording fakes: what runs in DllMain and
// what runs on the held entry, in which order. The licence rules: emf_content.dll is loaded only on the held entry
// before any chained DLL or EMF's core, it is unloaded before they start (or they do not start), and it is not
// loaded at all when the shadow copy is current.
#include <doctest.h>

#include <windows.h>

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "loader/content_splash.h"
#include "loader/content_start.h"

using emf::loader::ContentPath;
using emf::loader::Hooks;

namespace {
struct Recorder {
    std::vector<std::string> calls;
    std::vector<std::string> log;
    std::vector<emf::loader::ContentState> states;
    void add(const std::string& c) { calls.push_back(c); }
    std::string joined() const {
        std::string s;
        for (const auto& c : calls) s += (s.empty() ? "" : ", ") + c;
        return s;
    }
    bool logged(const std::string& part) const {
        for (const auto& l : log)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
};

const char* hooks_name(Hooks h) {
    switch (h) {
        case Hooks::Spike: return "hooks(spike)";
        case Hooks::ContentOn: return "hooks(content on)";
        default: return "hooks(content off)";
    }
}

// Everything there for a build: auto, the DLL, one mod, a good base, the client.
emf::loader::ContentFacts owned_facts() {
    emf::loader::ContentFacts f;
    f.dll_present = true;
    emf::loader::EmlMod m;
    m.folder = L"beasts";
    m.id = "beasts";
    m.version = "1.0.0";
    f.mods.usable.push_back(m);
    f.mods.skipped.emplace_back(L"rt", "needs EML's runtime loader, which EMF does not run");
    f.base.ok = true;
    f.base.dat_count = 55;
    f.base.why = "the base is enshrouded.kfc (55 containers)";
    return f;
}

emf::loader::ContentState good_state(const std::string& fp) {
    emf::loader::ContentState s;
    s.fingerprint = fp;
    s.code = 0;
    s.mods = {"beasts 1.0.0"};
    s.ms = 28000;
    s.built_at = "2026-11-01T20:00:00Z";
    return s;
}

emf::loader::PlanSteps plan_steps(Recorder& r, const emf::loader::ContentFacts& facts,
                                  std::optional<emf::loader::ContentState> state) {
    emf::loader::PlanSteps p;
    p.facts = [&r, facts] { return r.add("facts"), facts; };
    p.fingerprint = [&r] { return r.add("fingerprint"), std::string("sha256:now"); };
    p.read_state = [&r, state] { return r.add("read_state"), state; };
    p.log = [&r](const std::string& line) { r.log.push_back(line); };
    return p;
}

emf::loader::BuildOutcome built_ok() {
    emf::loader::BuildOutcome o;
    o.code = 0;
    o.message = "built";
    o.mods = {"beasts 1.0.0"};
    o.ms = 1500;
    return o;
}

emf::loader::StartSteps start_steps(Recorder& r, bool holdable = true, emf::loader::BuildOutcome outcome = built_ok(),
                                    bool shadow_ok = true, bool finishes = true) {
    emf::loader::StartSteps s;
    s.hold_entry = [&r, holdable] { return r.add("hold"), holdable; };
    s.run_chain = [&r] { r.add("chain"); };
    s.hooks = [&r](Hooks h) { r.add(hooks_name(h)); };
    s.start_core = [&r] { r.add("core"); };
    s.set_status = [&r](const emf::loader::ContentStatus& st) {
        r.add("status(" + st.kind + ")");
        r.log.push_back("status " + st.kind + " " + std::to_string(st.mods) + " " + std::to_string(st.ms) + " " + st.message);
    };
    s.log = [&r](const std::string& line) { r.log.push_back(line); };
    s.splash_begin = [&r](const std::string&) { r.add("splash"); };
    s.splash_end = [&r] { r.add("splash end"); };
    s.run_work = [&r, finishes](const std::function<void()>& work) {
        if (!finishes) return false;  // the work never came back in time
        work();
        return true;
    };
    s.build_shadow = [&r, shadow_ok] {
        r.add("build_shadow");
        emf::loader::ShadowResult res;
        res.ok = shadow_ok;
        res.why = shadow_ok ? "" : "the game folder's drive does not allow hard links";
        return res;
    };
    s.run_build = [&r, outcome](const std::atomic<bool>&) { return r.add("run_build"), outcome; };
    s.write_state = [&r](const emf::loader::ContentState& st) {
        r.add("write_state");
        r.states.push_back(st);
    };
    s.write_record = [&r](const std::string&) { r.add("write_record"); };
    s.drop_record = [&r] { r.add("drop_record"); };
    s.dll_gone = [] { return true; };
    s.alert = [&r](const std::string& text) {
        r.add("alert");
        r.log.push_back("alert " + text);
    };
    s.now = [] { return std::string("2026-11-01T21:00:00Z"); };
    return s;
}
}  // namespace

TEST_CASE("content start: the status text the core reads") {
    emf::loader::ContentStatus st;
    st.kind = "failed";
    st.mods = 3;
    st.ms = 28000;
    st.message = "EML could not\tpatch\r\nthe bear";
    const auto back = emf::loader::parse_content_status(emf::loader::content_status_text(st));
    REQUIRE(back.has_value());
    CHECK(back->kind == "failed");
    CHECK(back->mods == 3);
    CHECK(back->ms == 28000);
    CHECK(back->message == "EML could not patch the bear");  // one line, no tabs
    CHECK_FALSE(emf::loader::parse_content_status("").has_value());
    CHECK_FALSE(emf::loader::parse_content_status("built").has_value());
}

TEST_CASE("content start: off leaves today's order") {
    Recorder r;
    auto facts = owned_facts();
    facts.dll_present = false;
    auto plan = emf::loader::plan_content(plan_steps(r, facts, std::nullopt));
    CHECK(plan.path == ContentPath::Off);
    CHECK(r.joined() == "facts");  // no fingerprint, no state read
    CHECK(r.log == std::vector<std::string>{"EML content: off (emf_content.dll is not installed)"});
    r.calls.clear();
    auto steps = start_steps(r);
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "chain, hooks(spike), core");  // no hold; no status unless mode=on
    plan.mode_on = true;
    r.calls.clear();
    emf::loader::start_in_dllmain(plan, steps);
    CHECK(r.joined() == "chain, hooks(spike), status(off), core");
}

TEST_CASE("content start: current shadow redirects without loading the dll") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
    CHECK(plan.path == ContentPath::UseCurrent);
    CHECK(r.logged("the shadow copy is current"));
    r.calls.clear();
    auto steps = start_steps(r);
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "chain, hooks(content on), status(current), core");  // no hold, no build_shadow, no run_build
    CHECK(r.logged("status current 1 28000"));  // the state's mods and build time, for the core's line
}

TEST_CASE("content start: a state from other files, or none, builds again") {
    auto failed_before = good_state("sha256:before");
    failed_before.code = 2;
    for (const auto& state : {std::optional<emf::loader::ContentState>(), std::optional(good_state("sha256:before")),
                              std::optional(failed_before)}) {
        Recorder r;
        const auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), state));
        CHECK(plan.path == ContentPath::BuildHeld);
        CHECK(plan.fingerprint == "sha256:now");
        CHECK(plan.attempts == 1);
        CHECK(r.joined() == "facts, fingerprint, read_state");
        CHECK(r.logged("left out rt: needs EML's runtime loader"));
    }
}

TEST_CASE("content start: a build that failed with the same files is not tried again at every start") {
    auto failed = good_state("sha256:now");
    failed.code = 2;
    failed.message = "EML could not patch the bear";
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), failed));
    CHECK(plan.path == ContentPath::KnownFailed);
    CHECK(r.logged("EML content could not be built at an earlier start with the same files: EML could not patch the bear"));
    // the core was told at that start (and backed up the saves): now the warning only, no build, no redirect
    failed.reported = true;
    plan = emf::loader::plan_content(plan_steps(r, owned_facts(), failed));
    r.calls.clear();
    auto steps = start_steps(r);
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "chain, hooks(content off), status(failed_again), core");
    // a failure the core never heard of (the DLL stayed loaded, so the core did not start): told now, once
    failed.reported = false;
    failed.unloaded = false;
    plan = emf::loader::plan_content(plan_steps(r, owned_facts(), failed));
    REQUIRE(plan.path == ContentPath::KnownFailed);
    r.calls.clear();
    r.states.clear();
    emf::loader::start_in_dllmain(plan, steps);
    CHECK(r.joined() == "chain, hooks(content off), write_state, status(failed), core");
    CHECK(r.states.at(0).reported);
    CHECK(r.states.at(0).fingerprint == "sha256:now");
}

TEST_CASE("content start: a build the game stopped in is tried once more, then left alone") {
    auto running = good_state("sha256:now");
    running.code = emf::loader::kBuildRunning;
    running.attempts = 1;
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), running));
    CHECK(plan.path == ContentPath::BuildHeld);
    CHECK(plan.attempts == 2);
    CHECK(r.logged("the last build did not finish"));
    running.attempts = 2;
    plan = emf::loader::plan_content(plan_steps(r, owned_facts(), running));
    CHECK(plan.path == ContentPath::KnownFailed);
    CHECK(r.logged("stopped the game"));
}

TEST_CASE("content start: build runs before chain and core") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    REQUIRE(plan.path == ContentPath::BuildHeld);
    r.calls.clear();
    auto steps = start_steps(r);
    CHECK(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "hold");  // nothing else in DllMain: no chained DLL, no core
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    // a state that is never current before anything is touched (a crash leaves it), shadow.txt only after a good build
    CHECK(r.joined() == "splash, write_state, build_shadow, run_build, write_state, write_record, splash end, chain, "
                        "hooks(content on), status(built), core");
    REQUIRE(r.states.size() == 2);
    CHECK(r.states[0].fingerprint == "sha256:now");
    CHECK(r.states[0].code == emf::loader::kBuildRunning);
    CHECK(r.states[0].attempts == 1);
    CHECK(r.states[1].fingerprint == "sha256:now");
    CHECK(r.states[1].code == 0);
    CHECK(r.states[1].reported);
    CHECK(r.states[1].built_at == "2026-11-01T21:00:00Z");
    CHECK(r.logged("EML content: built 1 mod in 1.5 s"));
    CHECK(r.logged("status built 1 1500"));
}

TEST_CASE("content start: failed build starts chain and core without redirect") {
    auto failed = built_ok();
    failed.code = 2;
    failed.message = "EML could not patch the bear";
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r, true, failed);
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined() == "splash, write_state, build_shadow, run_build, write_state, splash end, drop_record, chain, "
                        "hooks(content off), status(failed), core");
    CHECK(r.states.at(1).code == 2);
    CHECK(r.states.at(1).reported);  // the core is told at this start
    CHECK(r.logged("EML content could not be built: EML could not patch the bear"));
    // a cut reply keeps code 0 but its result is unknown: no redirect either
    auto cut = built_ok();
    cut.truncated = true;
    Recorder c;
    auto cut_steps = start_steps(c, true, cut);
    emf::loader::run_held_content(plan, cut_steps);
    CHECK(c.joined().find("hooks(content off)") != std::string::npos);
    CHECK(c.states.at(1).truncated);
    // the shadow could not be made: the DLL is never loaded
    Recorder s;
    auto shadow_steps = start_steps(s, true, built_ok(), false);
    emf::loader::run_held_content(plan, shadow_steps);
    CHECK(s.joined() == "splash, write_state, build_shadow, write_state, splash end, drop_record, chain, "
                        "hooks(content off), status(failed), core");
    CHECK(s.states.at(1).code == emf::loader::kBuildShadowFailed);
    CHECK(s.states.at(1).message.find("hard links") != std::string::npos);
}

TEST_CASE("content start: dll not unloaded starts neither chain nor core") {
    auto stuck = built_ok();
    stuck.unloaded = false;
    stuck.message = "1 thread started by emf_content.dll still run, so it stays loaded";
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r, true, stuck);
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined() == "splash, write_state, build_shadow, run_build, write_state, splash end, drop_record, alert");
    CHECK(r.logged("without the chained DLLs and without EMF's core"));
    CHECK(r.logged("alert EMF could not finish preparing EML content"));  // the player sees why EMF's mods are missing
    CHECK_FALSE(r.states.at(1).unloaded);
    CHECK_FALSE(r.states.at(1).reported);  // the core does not start: it is told at the next start
}

TEST_CASE("content start: the DLL still in the process before the chained DLLs: neither starts") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r);
    steps.dll_gone = [] { return false; };  // the outcome said unloaded, but the module is still there
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined() == "splash, write_state, build_shadow, run_build, write_state, splash end, drop_record, alert");
    CHECK_FALSE(r.states.at(1).unloaded);
}

TEST_CASE("content start: a build that does not come back in time starts neither chain nor core") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r, true, built_ok(), true, false);
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined() == "splash, write_state, write_state, splash end, drop_record, alert");
    CHECK(r.logged("did not finish"));
    CHECK(r.states.at(1).code == emf::loader::kBuildTimedOut);
    CHECK_FALSE(r.states.at(1).unloaded);
}

TEST_CASE("content start: a build that comes back too late never loads the DLL") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r);
    std::function<void()> late;
    steps.run_work = [&late](const std::function<void()>& work) {
        late = work;  // the worker is still on its way when the time is up
        return false;
    };
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    REQUIRE(late);
    late();  // the worker goes on after the game started
    CHECK(r.joined().find("run_build") == std::string::npos);
}

TEST_CASE("content start: client entry not holdable turns content off") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    auto steps = start_steps(r, false);
    r.calls.clear();
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    // the player's EML mods are missing at this start: the core warns and backs up the saves, as after a failed build
    CHECK(r.joined() == "hold, chain, hooks(content off), status(failed), core");
    CHECK(plan.path == ContentPath::Off);
    CHECK(r.logged("EML content: off for this start"));
}

TEST_CASE("content start: facts stop at the first no (a start without emf_content.dll reads no DLL and no mod)") {
    std::vector<std::string> asked;
    emf::loader::FactsIo io;
    io.mode = [&] { return asked.push_back("mode"), emf::loader::ContentMode::Auto; };
    io.look.exists = [&](const std::wstring& name) { return asked.push_back("exists " + emf::loader::cio::to_utf8(name)), false; };
    io.look.tool_name = [&](const std::wstring& name) {
        return asked.push_back("read " + emf::loader::cio::to_utf8(name)), std::string();
    };
    io.look.chain = {L"creative.dll"};
    bool dll = false;
    io.dll_present = [&] { return asked.push_back("dll"), dll; };
    io.scan_mods = [&] { return asked.push_back("mods"), owned_facts().mods; };
    io.base = [&] { return asked.push_back("base"), owned_facts().base; };

    auto f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, false);
    CHECK_FALSE(emf::loader::decide_content(f).emf_owns);
    std::string seen;
    for (const auto& a : asked) seen += a + "; ";
    CHECK(seen == "mode; dll; exists dbghelp.dll; ");

    asked.clear();
    dll = true;
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, false);
    CHECK(emf::loader::decide_content(f).emf_owns);
    seen.clear();
    for (const auto& a : asked) seen += a + "; ";
    CHECK(seen == "mode; dll; exists dbghelp.dll; mods; read creative.dll; base; ");

    asked.clear();
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::NoMods, false);
    CHECK(asked == std::vector<std::string>{"mode"});
    asked.clear();
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, true);  // the server: the same questions
    CHECK(f.server);
    CHECK(emf::loader::decide_content(f).emf_owns);
    seen.clear();
    for (const auto& a : asked) seen += a + "; ";
    CHECK(seen == "mode; dll; exists dbghelp.dll; mods; read creative.dll; base; ");

    // EML's or ShroudForge's dbghelp.dll there but no emf_content.dll: named, never read (it is up to 32 MB)
    asked.clear();
    dll = false;
    io.look.exists = [&](const std::wstring& name) { return asked.push_back("exists " + emf::loader::cio::to_utf8(name)), true; };
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, false);
    CHECK(f.other_tool == std::optional<std::string>("dbghelp.dll (EML or ShroudForge)"));
    CHECK(asked == std::vector<std::string>{"mode", "dll", "exists dbghelp.dll"});
    asked.clear();
    dll = true;
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, false);
    CHECK(asked == std::vector<std::string>{"mode", "dll", "exists dbghelp.dll", "read dbghelp.dll"});

    // another program that loads EMF's loader: nothing more is asked
    asked.clear();
    f = emf::loader::collect_facts(io, emf::loader::PlayOnce::Normal, false, false);
    CHECK(asked == std::vector<std::string>{"mode"});
    CHECK(emf::loader::decide_content(f).why.find("not the game") != std::string::npos);
}

TEST_CASE("content splash: a window while the work runs on another thread, with its last line; gone afterwards") {
    emf::loader::ContentSplash splash;
    int shown = 0;
    splash.show_window = [&](HWND) { ++shown; };  // nothing on the screen during the tests
    REQUIRE(splash.begin("EMF is preparing EML content (first start after a change)..."));
    const HWND w = splash.window();
    CHECK(IsWindow(w));
    CHECK_FALSE(IsWindowVisible(w));  // hidden until the work has run for a second
    CHECK((GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);  // never takes the game's focus
    DWORD worker = 0;
    const bool done = emf::loader::run_with_splash(&splash,
                                                   [&] {
                                                       worker = GetCurrentThreadId();
                                                       splash.line("standin: building");
                                                       Sleep(300);
                                                       splash.line("the last line");
                                                   },
                                                   10000);
    CHECK(done);
    CHECK(worker != 0);
    CHECK(worker != GetCurrentThreadId());
    CHECK(splash.last_line() == "the last line");
    CHECK(shown == 0);  // a work of 0.3 s never shows it: a quick start shows nothing
    CHECK_FALSE(splash.shown());
    splash.end();
    CHECK_FALSE(IsWindow(w));
    // a work that does not come back in time: false, and the work keeps what it needs
    CHECK_FALSE(emf::loader::run_with_splash(nullptr, [] { Sleep(1500); }, 100));
}

TEST_CASE("content splash: a work longer than the delay shows the window once, after the delay") {
    emf::loader::ContentSplash splash;
    std::vector<ULONGLONG> shown_at;
    splash.show_window = [&](HWND) { shown_at.push_back(GetTickCount64()); };
    const ULONGLONG start = GetTickCount64();
    REQUIRE(splash.begin("EMF is preparing EML content (first start after a change)...", 150));
    CHECK(emf::loader::run_with_splash(&splash, [] { Sleep(700); }, 10000));
    REQUIRE(shown_at.size() == 1u);
    CHECK(shown_at[0] - start >= 140u);  // not before the delay
    CHECK(shown_at[0] - start < 650u);   // but while the work still ran
    CHECK(splash.shown());
    splash.end();
}

TEST_CASE("content splash: without a splash (the dedicated server) it only waits: no message loop, no window") {
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);  // this thread gets its message queue
    REQUIRE(PostThreadMessageW(GetCurrentThreadId(), WM_APP + 7, 1, 2));
    DWORD worker = 0;
    CHECK(emf::loader::run_with_splash(nullptr, [&] { worker = GetCurrentThreadId(); Sleep(200); }, 10000));
    CHECK(worker != 0);
    CHECK(worker != GetCurrentThreadId());
    // the message is still there: nothing pumped this thread's queue
    CHECK(PeekMessageW(&m, nullptr, WM_APP + 7, WM_APP + 7, PM_REMOVE));
}

TEST_CASE("content start: a current state but a shadow whose record does not match the game builds again") {
    Recorder r;
    auto steps = plan_steps(r, owned_facts(), good_state("sha256:now"));
    steps.record_ok = [&r] { return r.add("record"), false; };  // shadow.txt gone, or the game's files changed
    const auto plan = emf::loader::plan_content(steps);
    CHECK(plan.path == ContentPath::BuildHeld);
    CHECK(r.joined() == "facts, fingerprint, read_state, record");
    CHECK(r.logged("its record, shadow.txt, is missing"));
    Recorder ok;
    auto good = plan_steps(ok, owned_facts(), good_state("sha256:now"));
    good.record_ok = [&ok] { return ok.add("record"), true; };
    CHECK(emf::loader::plan_content(good).path == ContentPath::UseCurrent);
}

TEST_CASE("content splash: its size follows the screen's DPI") {
    CHECK(emf::loader::splash_scaled(620, 96) == 620);
    CHECK(emf::loader::splash_scaled(620, 144) == 930);  // 150 %
    CHECK(emf::loader::splash_scaled(110, 192) == 220);  // 200 %
    emf::loader::ContentSplash splash;
    REQUIRE(splash.begin("EMF is preparing EML content (first start after a change)..."));
    RECT r{};
    GetWindowRect(splash.window(), &r);
    CHECK(r.right - r.left == emf::loader::splash_scaled(620, emf::loader::screen_dpi()));
    splash.end();
}

namespace {
emf::loader::PortraitItem wolf_item() {
    emf::loader::PortraitItem p;
    p.mod = "beastmaster";
    p.key = "wolf";
    p.template_guid = "503fa1d1-dce2-45a1-85a5-185a22666d33";
    return p;
}
void with_portraits(Recorder& r, emf::loader::StartSteps& s, bool unloads = true) {
    s.run_portraits = [&r, unloads](const std::vector<emf::loader::PortraitItem>& items, bool shadow,
                                    const std::atomic<bool>&) {
        r.add(std::string("portraits(") + (shadow ? "shadow" : "game") + ")");
        emf::loader::PortraitsOutcome o;
        o.call.code = 0;
        o.call.unloaded = unloads;
        for (const auto& i : items) o.drawn.push_back(i.key);
        return o;
    };
}
}  // namespace

TEST_CASE("content start: missing portraits on a current shadow hold the start, are drawn from it, then all goes on") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
    REQUIRE(plan.path == ContentPath::UseCurrent);
    plan.portraits = {wolf_item()};
    r.calls.clear();
    auto steps = start_steps(r);
    with_portraits(r, steps);
    CHECK(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "hold");  // the chained DLLs and the core wait for the pictures
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined() == "splash, portraits(shadow), splash end, chain, hooks(content on), status(current), core");
    CHECK(r.logged("drew 1"));
}

TEST_CASE("content start: no portraits missing: no hold (a start with every picture cached is not slower)") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
    r.calls.clear();
    auto steps = start_steps(r);
    with_portraits(r, steps);
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "chain, hooks(content on), status(current), core");
}

TEST_CASE("content start: a build start draws the portraits after the build: from the shadow when it built, else the game") {
    for (const bool ok : {true, false}) {
        Recorder r;
        auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
        REQUIRE(plan.path == ContentPath::BuildHeld);
        plan.portraits = {wolf_item()};
        emf::loader::BuildOutcome failed = built_ok();
        failed.code = 2;
        auto steps = start_steps(r, true, ok ? built_ok() : failed);
        with_portraits(r, steps);
        CHECK(emf::loader::start_in_dllmain(plan, steps));
        r.calls.clear();
        emf::loader::run_held_content(plan, steps);
        CHECK(r.joined().find(ok ? "run_build, portraits(shadow)" : "run_build, portraits(game)") != std::string::npos);
        CHECK(r.joined().find("core") != std::string::npos);
    }
}

TEST_CASE("content start: portraits that leave emf_content.dll in the game start neither the chained DLLs nor the core") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
    plan.portraits = {wolf_item()};
    auto steps = start_steps(r);
    with_portraits(r, steps, false);
    CHECK(emf::loader::start_in_dllmain(plan, steps));
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    CHECK(r.joined().find("chain") == std::string::npos);
    CHECK(r.joined().find("core") == std::string::npos);
    CHECK(r.joined().find("alert") != std::string::npos);
}

TEST_CASE("content start: a start that cannot be held draws no portraits and goes on as before") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
    plan.portraits = {wolf_item()};
    r.calls.clear();
    auto steps = start_steps(r, false);
    with_portraits(r, steps);
    CHECK_FALSE(emf::loader::start_in_dllmain(plan, steps));
    CHECK(r.joined() == "hold, chain, hooks(content on), status(current), core");
    CHECK(r.logged("later start"));
}

TEST_CASE("content start: pictures that leave emf_content.dll loaded after a good build keep the build's own state") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    REQUIRE(plan.path == ContentPath::BuildHeld);
    plan.portraits = {wolf_item()};
    auto steps = start_steps(r);
    with_portraits(r, steps, false);  // the build unloads the DLL, the pictures call does not
    CHECK(emf::loader::start_in_dllmain(plan, steps));
    r.calls.clear();
    emf::loader::run_held_content(plan, steps);
    REQUIRE_FALSE(r.states.empty());
    const auto& last = r.states.back();
    CHECK(last.code == 0);
    CHECK(last.unloaded);  // a good build stays a good build: the next start finds the shadow copy current
    CHECK(emf::loader::state_current(last, "sha256:now"));
    CHECK(last.message.find("pictures") == std::string::npos);
    CHECK(r.joined().find("write_record") != std::string::npos);
    CHECK(r.joined().find("drop_record") == std::string::npos);  // the shadow copy is good: it stays redirected next start
    CHECK(r.joined().find("chain") == std::string::npos);  // only this start goes without the chained DLLs and the core
    CHECK(r.joined().find("core") == std::string::npos);
    CHECK(r.joined().find("alert") != std::string::npos);
    CHECK(r.logged("pictures"));
    CHECK(r.logged("without the chained DLLs"));
}

TEST_CASE("content start: every pictures call is recorded for its items, also one that did not come back") {
    for (const bool finishes : {true, false}) {
        Recorder r;
        auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), good_state("sha256:now")));
        plan.portraits = {wolf_item()};
        auto steps = start_steps(r, true, built_ok(), true, finishes);
        with_portraits(r, steps);
        std::vector<std::string> recorded;
        int code = -1;
        steps.record_portraits = [&](const std::vector<emf::loader::PortraitItem>& items, const emf::loader::PortraitsOutcome& o) {
            for (const auto& i : items) recorded.push_back(i.key);
            code = o.call.code;
        };
        CHECK(emf::loader::start_in_dllmain(plan, steps));
        emf::loader::run_held_content(plan, steps);
        REQUIRE(recorded.size() == 1u);
        CHECK(recorded[0] == "wolf");
        CHECK(code == (finishes ? 0 : emf::loader::kBuildTimedOut));
    }
}

TEST_CASE("content start: the pictures after a build are recorded too") {
    Recorder r;
    auto plan = emf::loader::plan_content(plan_steps(r, owned_facts(), std::nullopt));
    plan.portraits = {wolf_item()};
    auto steps = start_steps(r);
    with_portraits(r, steps);
    int recorded = 0;
    steps.record_portraits = [&](const std::vector<emf::loader::PortraitItem>&, const emf::loader::PortraitsOutcome&) {
        ++recorded;
    };
    CHECK(emf::loader::start_in_dllmain(plan, steps));
    emf::loader::run_held_content(plan, steps);
    CHECK(recorded == 1);
}
