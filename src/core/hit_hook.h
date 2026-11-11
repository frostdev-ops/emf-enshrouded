#pragma once
// Hits before they land (API v31): a detour on the game's applyHitToEntity (Sig::ApplyHitToEntity) runs the mods'
// rules and before-callbacks once per landing entity hit on the server world, on the impact thread inside the step
// (private research hit_hooks_2026-11-11 section 2.1). pre_hit is the game-free part the tests drive.
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/hits.h"
#include "emf/emf_api.h"

namespace emf::core {

struct HitStats {
    std::atomic<uint64_t> seen{0};       // hits that ran the rules and callbacks
    std::atomic<uint64_t> changed{0};    // of them, written back changed
    std::atomic<uint64_t> cancelled{0};  // of them, cancelled
    std::atomic<uint64_t> faults{0};     // faults in a callback or the hook's reads
    std::atomic<uint64_t> ns{0};         // time spent in the hook's work
    void reset();
};

// The mods' hit callbacks: before (inline on the impact thread) and after (after the server world's step). Never
// removed; a callback that faults is switched off and logged once with its module. Thread-safe.
class HitCallbacks {
public:
    static constexpr size_t kMax = 16;
    bool add_before(EmfHitBeforeCallback cb, void* user, std::string module);  // false: null, or 16 already
    bool add_after(EmfHitCallback cb, void* user, std::string module);
    size_t before_count() const;
    size_t after_count() const;
    void call_before(EmfWorld* w, EmfHit& h, HitStats& st);
    void call_after(EmfWorld* w, const EmfHit& h);
    void set_log(std::function<void(const std::string&)> log);
    void reset();  // tests

private:
    template <class Cb>
    struct Entry {
        Cb cb = nullptr;
        void* user = nullptr;
        std::string module;
        std::atomic<bool> on{true};
    };
    void fault(const std::string& module, const char* which);
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::array<Entry<EmfHitBeforeCallback>, kMax> before_;
    std::array<Entry<EmfHitCallback>, kMax> after_;
    size_t n_before_ = 0;
    size_t n_after_ = 0;
    std::function<void(const std::string&)> log_;
};

// What the hook needs to know about the server world's entities (api.cpp fills it from the game).
struct PreHitEnv {
    EmfWorld* world = nullptr;
    uint8_t world_kind = 0;
    void* ctx = nullptr;
    HitClassify classify = nullptr;
    bool (*applies)(uint32_t entity, void* ctx) = nullptr;        // takes hits: no CombatFlags or handleHits on
    int32_t (*health)(uint32_t entity, void* ctx) = nullptr;      // its health now, -1 unknown
    uint32_t (*weapon_item)(uint32_t pide, void* ctx) = nullptr;  // a weapon's item id, 0 unknown
};

// One applyHitToEntity call: on the server world, for an entity hit, with rules or before-callbacks, at the first
// level of a propagated hit that applies it: builds the EmfHit, runs the rules and the callbacks, writes it back when
// changed. True when the hit changed.
bool pre_hit(uint8_t* hc, const PreHitEnv& env, HitOnce& once, const HitRules& rules, HitCallbacks& cbs, HitStats& st);

// Installs the detour (through the hook table); false when it could not be installed.
bool install_hit_hook(uintptr_t fn);
bool hit_hook_on();
// A new impact_hit_conclusion run on this thread (its pre-hook): the once-per-hit tracker starts over.
void hit_hook_new_step();

}  // namespace emf::core
