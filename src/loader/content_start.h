#pragma once
// The content loader's start sequence (docs/engine_parts/content_loader.md), with every side effect injected so the
// tests can check the order. The licence rules it keeps:
//   1. emf_content.dll is loaded only on the held entry (the main thread, the loader lock free, before any game code),
//      before any chained DLL, EMF's core or a mod;
//   2. it is unloaded before they start; when it cannot be (a thread of its own still runs, or the build does not come
//      back), this start runs without the chained DLLs and without EMF's core, and says why;
//   3. it is never loaded when the fingerprint and the state say the shadow copy is current.
//
// In DllMain: plan_content (the facts, the decision, the fingerprint and the state), then start_in_dllmain:
//   Off          today's order: the chained DLLs, the file hooks of the redirect spike's switches, the core
//   UseCurrent   the same, with the hooks sending the game's data opens to the shadow copy
//   BuildHeld    only the hold of the exe's entry; everything else runs there (run_held_content). If the entry
//                cannot be held, content is off for this start and the order is today's.
// On the held entry, run_held_content: the splash, the shadow and the build (on a worker thread while the splash is
// kept alive), the state, then the chained DLLs, the hooks (redirect on only after a good build) and the core.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "content_build.h"
#include "content_owner.h"
#include "content_shadow.h"
#include "content_state.h"
#include "content_status_env.h"

namespace emf::loader {

enum class ContentPath { Off, UseCurrent, BuildHeld };

struct ContentPlan {
    ContentPath path = ContentPath::Off;
    std::string why;          // the decision's reason
    bool emf_owns = false;    // EMF applies the content at this start (UseCurrent or BuildHeld)
    bool mode_on = false;     // [content] mode=on: the core shows why content is off
    std::string fingerprint;  // when EMF owns it
    size_t state_mods = 0;    // UseCurrent: the earlier build's mods and time
    uint64_t state_ms = 0;
};

// ---- the facts, gathered in the order of the checks: the first that says no ends it ----

struct FactsIo {
    std::function<ContentMode()> mode;
    ToolLook look;
    std::function<bool()> dll_present;
    std::function<ModScan()> scan_mods;
    std::function<BaseChoice()> base;
};

// Asks only what the decision needs: a start without emf_content.dll looks for the DLL and another tool's dbghelp.dll
// and reads no DLL and no mod folder. Reading the chained DLLs' text comes after the mods, just before the base.
// `game`: the exe is the game client or the dedicated server.
inline ContentFacts collect_facts(const FactsIo& io, PlayOnce play, bool server, bool game = true) {
    ContentFacts f;
    f.server = server;
    f.play = play;
    f.game = game;
    f.mode = io.mode ? io.mode() : ContentMode::Auto;
    if (f.mode == ContentMode::Off || play == PlayOnce::NoMods || !game) return f;
    f.dll_present = io.dll_present && io.dll_present();
    f.other_tool = other_content_tool(io.look, f.dll_present, false);  // dbghelp.dll read only with the DLL there
    if (f.other_tool || !f.dll_present || server) return f;
    if (io.scan_mods) f.mods = io.scan_mods();
    if (f.mods.usable.empty()) return f;
    f.other_tool = chained_tool_by_text(io.look);
    if (f.other_tool) return f;
    if (io.base) f.base = io.base();
    return f;
}

// ---- in DllMain ----

struct PlanSteps {
    std::function<ContentFacts()> facts;
    std::function<std::string()> fingerprint;  // asked only when EMF owns the content
    std::function<std::optional<ContentState>()> read_state;
    // the redirect's own check of shadow.txt against the game's files (file_redirect.h) passes; asked only for a
    // current state, so a "current" start is one the redirect will really take (null: not asked)
    std::function<bool()> record_ok;
    std::function<void(const std::string&)> log;
};

inline std::string seconds_text(uint64_t ms) {
    if (ms < 10000) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f s", static_cast<double>(ms) / 1000.0);
        return buf;
    }
    return std::to_string((ms + 500) / 1000) + " s";
}

inline std::string mods_text(size_t n) { return std::to_string(n) + (n == 1 ? " mod" : " mods"); }

inline ContentPlan plan_content(const PlanSteps& s) {
    ContentPlan plan;
    const ContentFacts facts = s.facts();
    plan.mode_on = facts.mode == ContentMode::On;
    const ContentDecision d = decide_content(facts);
    plan.why = d.why;
    if (facts.dll_present)  // the mods are looked at only then
        for (const auto& [folder, reason] : facts.mods.skipped)
            s.log("EML content: left out " + cio::to_utf8(folder) + ": " + reason);
    if (!d.emf_owns) {
        s.log("EML content: " + d.why);
        return plan;
    }
    plan.emf_owns = true;
    plan.fingerprint = s.fingerprint();
    const std::optional<ContentState> state = s.read_state();
    const bool current = state_current(state, plan.fingerprint);
    if (current && s.record_ok && !s.record_ok()) {
        plan.path = ContentPath::BuildHeld;
        s.log("EML content: " + d.why + "; a build is needed at this start (the shadow copy's record, shadow.txt, is "
              "missing or does not match the game's files)");
        return plan;
    }
    if (current) {
        plan.path = ContentPath::UseCurrent;
        plan.state_mods = state->mods.size();
        plan.state_ms = state->ms;
        s.log("EML content: the shadow copy is current (" + mods_text(state->mods.size()) + ", built " + state->built_at +
              "): emf_content.dll is not loaded");
        return plan;
    }
    plan.path = ContentPath::BuildHeld;
    const std::string reason = !state ? "no earlier build"
                               : (state->code != 0 || !state->unloaded || state->truncated)
                                   ? "the last build did not work"
                                   : "the game, the mods or emf_content.dll changed since the last build";
    s.log("EML content: " + d.why + "; a build is needed at this start (" + reason + ")");
    return plan;
}

// The file hooks: the redirect spike's switches as emf_core.ini has them (content off), or the redirect to the shadow
// copy on (after a good build, or a current shadow) or forced off (EMF owns the content but has no good build).
enum class Hooks { Spike, ContentOn, ContentOff };

struct StartSteps {
    // DllMain
    std::function<bool()> hold_entry;  // holds the exe's entry for run_held_content; false (and says why) when it cannot
    // DllMain or the held entry
    std::function<void()> run_chain;
    std::function<void(Hooks)> hooks;
    std::function<void()> start_core;
    std::function<void(const ContentStatus&)> set_status;
    std::function<void(const std::string&)> log;
    // the held entry
    std::function<void(const std::string&)> splash_begin;
    std::function<void()> splash_end;
    // runs `work` (on a worker thread while the splash is kept alive); false when it did not come back in time
    std::function<bool(const std::function<void()>& work)> run_work;
    std::function<ShadowResult()> build_shadow;
    std::function<BuildOutcome()> run_build;  // load, build, unload (content_build.h), on the thread run_work gives
    std::function<void(const ContentState&)> write_state;
    std::function<void()> drop_record;  // removes shadow.txt: a shadow without a good build must never be redirected to
    std::function<std::string()> now;   // UTC, for built_at
};

// True when the entry is held: the rest runs there (run_held_content), nothing more here.
inline bool start_in_dllmain(ContentPlan& plan, const StartSteps& s) {
    std::optional<ContentStatus> status;
    if (plan.path == ContentPath::BuildHeld) {
        if (s.hold_entry()) return true;
        plan.path = ContentPath::Off;
        plan.why = "off for this start: the game's start could not be held for the build";
        s.log("EML content: " + plan.why);
        status = ContentStatus{"failed", 0, 0, "EMF could not hold the game's start to build the EML content"};
    } else if (plan.path == ContentPath::UseCurrent) {
        status = ContentStatus{"current", plan.state_mods, plan.state_ms, ""};
    } else if (plan.mode_on) {
        status = ContentStatus{"off", 0, 0, plan.why};
    }
    s.run_chain();
    s.hooks(plan.path == ContentPath::UseCurrent ? Hooks::ContentOn : Hooks::Spike);
    if (status) s.set_status(*status);
    s.start_core();
    return false;
}

constexpr uint32_t kBuildTimeoutMs = 15 * 60 * 1000;

// On the held entry (BuildHeld).
inline void run_held_content(const ContentPlan& plan, const StartSteps& s) {
    s.splash_begin("EMF is preparing EML content (first start after a change)...");
    struct Work {
        BuildOutcome out;
    };
    // the worker owns its result too: one that does not come back in time must not write into a dead frame
    auto work = std::make_shared<Work>();
    const bool done = s.run_work([work, &s] {
        const ShadowResult shadow = s.build_shadow();
        if (!shadow.ok) {
            work->out.code = kBuildShadowFailed;
            work->out.message = shadow.why;
            work->out.unloaded = true;  // never loaded
            return;
        }
        work->out = s.run_build();
    });
    BuildOutcome out;
    if (done) {
        out = work->out;
    } else {
        out.code = kBuildError;
        out.unloaded = false;
        out.message = "the build did not finish in " + std::to_string(kBuildTimeoutMs / 60000) +
                      " minutes, so emf_content.dll may still run";
    }
    ContentState state;
    state.fingerprint = plan.fingerprint;
    state.code = out.code;
    state.message = out.message;
    state.mods = out.mods;
    state.errors = out.errors;
    state.ms = out.ms;
    state.built_at = s.now();
    state.unloaded = out.unloaded;
    state.truncated = out.truncated;
    s.write_state(state);
    s.splash_end();
    if (!out.unloaded) {
        s.drop_record();
        s.log("EML content: " + out.message + ": this start runs without the chained DLLs and without EMF's core (EML's "
              "code and the mods are never in the game together)");
        return;
    }
    const bool ok = out.usable();
    if (ok) {
        s.log("EML content: built " + mods_text(out.mods.size()) + " in " + seconds_text(out.ms) +
              "; the game reads the shadow copy");
    } else {
        s.drop_record();
        s.log("EML content could not be built: " + out.message + " (code " + std::to_string(out.code) +
              "); the game starts without it (emf\\content.log has the details)");
        for (const auto& e : out.errors) s.log("EML content: " + e);
    }
    s.run_chain();
    s.hooks(ok ? Hooks::ContentOn : Hooks::ContentOff);
    s.set_status(ok ? ContentStatus{"built", out.mods.size(), out.ms, ""}
                    : ContentStatus{"failed", out.mods.size(), out.ms, out.message});
    s.start_core();
}

}  // namespace emf::loader
