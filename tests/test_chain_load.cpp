// The loader starts the chained DLLs in list order; one that is missing or fails is reported and skipped, never
// stops the game. And the one-shot flag of EMF.exe's "Play without EMF mods" (play_once.h).
#include <doctest.h>

#include <optional>
#include <string>
#include <vector>

#include "loader/chain_load.h"
#include "loader/play_once.h"

using emf::loader::ChainResult;
using emf::loader::load_chain;

TEST_CASE("chain load: every DLL in order; missing and failing ones are reported and the rest still load") {
    std::vector<std::wstring> tried;
    const auto results = load_chain(
        {L"a.dll", L"gone.dll", L"bad.dll", L"b.dll"}, [](const std::wstring& n) { return n != L"gone.dll"; },
        [&](const std::wstring& n, unsigned long* error) {
            tried.push_back(n);
            if (n == L"bad.dll") {
                *error = 193;  // ERROR_BAD_EXE_FORMAT: a 32-bit DLL
                return false;
            }
            return true;
        });
    CHECK(tried == std::vector<std::wstring>{L"a.dll", L"bad.dll", L"b.dll"});
    REQUIRE(results.size() == 4);
    CHECK(results[0].kind == ChainResult::Loaded);
    CHECK(results[1].kind == ChainResult::Missing);
    CHECK(results[2].kind == ChainResult::Failed);
    CHECK(results[2].error == 193);
    CHECK(results[3].kind == ChainResult::Loaded);
}

TEST_CASE("chain load: the log line names the DLL and what happened") {
    CHECK(emf::loader::chain_line({L"creative.dll", ChainResult::Loaded, 0}) == "chain: started creative.dll");
    CHECK(emf::loader::chain_line({L"gone.dll", ChainResult::Missing, 0}) ==
          "chain: gone.dll is listed but not in the game folder; skipped");
    CHECK(emf::loader::chain_line({L"bad.dll", ChainResult::Failed, 193}) ==
          "chain: bad.dll did not load (Windows error 193); skipped");
}

TEST_CASE("chain load: the status file has one tab-separated line per DLL for EMF's manager") {
    const std::string status = emf::loader::chain_status_text(
        {{L"a.dll", ChainResult::Loaded, 0}, {L"gone.dll", ChainResult::Missing, 0}, {L"bad.dll", ChainResult::Failed, 5}});
    CHECK(status == "a.dll\tstarted\r\ngone.dll\tmissing\r\nbad.dll\tfailed 5\r\n");
}

TEST_CASE("chain load: a non-ASCII name is written in UTF-8") {
    CHECK(emf::loader::chain_line({L"möd.dll", ChainResult::Loaded, 0}) == "chain: started m\xC3\xB6" "d.dll");
}

// ---- "Play without EMF mods" (EMF.exe's Play menu): the one-shot flag file the loader reads (play_once.h) ----

namespace {
using emf::loader::PlayOnce;
constexpr int64_t kT = 1793520000;  // when EMF.exe wrote the flag
PlayOnce mode_at(const std::string& text, int64_t now) { return emf::loader::parse_play_once(text, now).mode; }
}  // namespace

TEST_CASE("play once: EMF.exe's flag counts for a start within ten minutes of being written, never later") {
    CHECK(emf::loader::play_once_text(PlayOnce::NoEmf, kT) == "emf_play_once=1\r\nmode=no-emf\r\nwritten=1793520000\r\n");
    CHECK(emf::loader::play_once_text(PlayOnce::NoMods, kT) == "emf_play_once=1\r\nmode=no-mods\r\nwritten=1793520000\r\n");
    for (const PlayOnce mode : {PlayOnce::NoEmf, PlayOnce::NoMods}) {
        const std::string text = emf::loader::play_once_text(mode, kT);
        CHECK(mode_at(text, kT) == mode);
        CHECK(mode_at(text, kT + 450) == mode);   // a cold Steam start with an update can take minutes
        CHECK(mode_at(text, kT + 599) == mode);
        CHECK(mode_at(text, kT + 601) == PlayOnce::Normal);   // a start that never happened: EMF loads again
        CHECK(mode_at(text, kT + 86400) == PlayOnce::Normal);
        CHECK(mode_at(text, kT - 60) == mode);                // the clock went back a little since
        CHECK(mode_at(text, kT - 3600) == PlayOnce::Normal);  // "written" an hour from now: not trusted
    }
    // a BOM and LF line ends (written by hand) are fine; anything that is not EMF.exe's flag counts as no flag
    CHECK(mode_at("\xEF\xBB\xBF" "emf_play_once=1\nmode=no-mods\nwritten=1793520000\n", kT) == PlayOnce::NoMods);
    CHECK(mode_at("", kT) == PlayOnce::Normal);
    CHECK(mode_at("mode=no-emf\r\nwritten=1793520000\r\n", kT) == PlayOnce::Normal);  // no emf_play_once line
    CHECK(mode_at("emf_play_once=1\nmode=sideways\nwritten=1793520000\n", kT) == PlayOnce::Normal);
    CHECK(mode_at("emf_play_once=1\nmode=no-emf\n", kT) == PlayOnce::Normal);  // no time: it could be from any day
    CHECK(mode_at("emf_play_once=1\nmode=no-emf\nwritten=17935x0000\n", kT) == PlayOnce::Normal);
    CHECK(mode_at("emf_play_once=1\nmode=no-emf\nwritten=99999999999999999999999\n", kT) == PlayOnce::Normal);
    CHECK(mode_at("emf_play_once=2\nmode=no-emf\nwritten=1793520000\n", kT) == PlayOnce::Normal);  // a newer format
    // the log says what happened
    const auto obeyed = emf::loader::parse_play_once(emf::loader::play_once_text(PlayOnce::NoEmf, kT), kT + 20);
    CHECK(obeyed.why.find("without EMF mods") != std::string::npos);
    CHECK(obeyed.why.find("dinput8-chain.ini") != std::string::npos);
    CHECK(obeyed.why.find("next start") != std::string::npos);
    const auto old = emf::loader::parse_play_once(emf::loader::play_once_text(PlayOnce::NoEmf, kT), kT + 900);
    CHECK(old.why.find("15 minutes ago") != std::string::npos);
    CHECK(emf::loader::parse_play_once("", kT).why.empty() == false);
}

TEST_CASE("play once: the loader deletes the flag first, whatever it holds; a flag it cannot delete is not obeyed") {
    const std::string flag = emf::loader::play_once_text(PlayOnce::NoMods, kT);
    std::vector<std::string> steps;
    const auto take = [&](std::optional<std::string> text, bool removable, int64_t now) {
        steps.clear();
        return emf::loader::take_play_once(
            [&] {
                steps.push_back("read");
                return text;
            },
            [&] {
                steps.push_back("remove");
                return removable;
            },
            now);
    };
    auto r = take(flag, true, kT + 5);
    CHECK(r.mode == PlayOnce::NoMods);
    CHECK(steps == std::vector<std::string>{"read", "remove"});
    r = take(flag, true, kT + 3600);  // too old: deleted all the same
    CHECK(r.mode == PlayOnce::Normal);
    CHECK(steps == std::vector<std::string>{"read", "remove"});
    CHECK(r.why.find("deleted") != std::string::npos);
    r = take(flag, false, kT + 3600);  // the log says what happened: it could not be deleted
    CHECK(r.mode == PlayOnce::Normal);
    CHECK(r.why.find("could not delete") != std::string::npos);
    CHECK(r.why.find("and deleted") == std::string::npos);
    r = take(std::string("not a flag"), true, kT);
    CHECK(r.mode == PlayOnce::Normal);
    CHECK(steps == std::vector<std::string>{"read", "remove"});
    r = take(std::nullopt, true, kT);  // no file: nothing to delete, nothing to say
    CHECK(r.mode == PlayOnce::Normal);
    CHECK(r.why.empty());
    CHECK(steps == std::vector<std::string>{"read"});
    r = take(flag, false, kT + 5);  // it would stay for the next start: EMF starts as usual instead
    CHECK(r.mode == PlayOnce::Normal);
    CHECK(r.why.find("could not delete") != std::string::npos);
}

TEST_CASE("play once: what starts: everything; without EMF the chained DLL mods still; without any mods nothing") {
    CHECK(emf::loader::start_plan(PlayOnce::Normal).chain);
    CHECK(emf::loader::start_plan(PlayOnce::Normal).core);
    CHECK(emf::loader::start_plan(PlayOnce::NoEmf).chain);
    CHECK_FALSE(emf::loader::start_plan(PlayOnce::NoEmf).core);
    CHECK_FALSE(emf::loader::start_plan(PlayOnce::NoMods).chain);
    CHECK_FALSE(emf::loader::start_plan(PlayOnce::NoMods).core);
    // the chained DLLs a start without mods skips are named so in the log and in emf\chain_status.txt
    const auto skipped = emf::loader::skip_chain({L"creative.dll", L"eml.dll"});
    REQUIRE(skipped.size() == 2);
    CHECK(skipped[0].kind == ChainResult::Skipped);
    CHECK(emf::loader::chain_status_text(skipped) == "creative.dll\tskipped\r\neml.dll\tskipped\r\n");
    CHECK(emf::loader::chain_line(skipped[1]) == "chain: eml.dll not started: this start is without mods");
}
