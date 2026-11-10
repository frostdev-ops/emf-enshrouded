#pragma once
// The HUD editor's hooks on the game's UI scope open and close (docs/engine_parts/re_ui_scopes.md). Per scope the detour
// does one lookup on the game's own name hash; for a managed part it adds the part's offset to the node's position just
// before the original close (hide subtracts 20000), and it pushes a scale transform after the original open and pops it
// after the original close. The detours read an immutable HudSnapshot: no locks, no allocation, no logging.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "common/signatures.h"
#include "core/hud_editor/scale.h"
#include "core/hud_editor/snapshot.h"

namespace emf::core::hud {

constexpr size_t kScopeNode = 0x08, kScopeOutline = 0x20, kNodePos = 0x70, kNodeRect = 0x114;
constexpr float kHideShift = 20000.0f;

struct NameInfo {
    const char* name;
    size_t len;
    uint64_t hash;
};
using OpenFn = void* (*)(void* scope, const NameInfo* ni);
using CloseFn = void (*)(void* scope);

struct OpenScope {
    const void* scope = nullptr;
    const void* node = nullptr;
    int element = -1;
    ScalePush push;
};

// The scopes opened and not yet closed that we manage; one per thread (thread_local in the detours).
class ScopeTracker {
public:
    static constexpr int kDepth = 32;
    bool has_room() const { return depth_ < kDepth; }
    void opened(const OpenScope& s);  // ignored when full (the caller checks has_room before it pushes a transform)
    struct Closed {
        bool found = false;
        OpenScope scope;
        OpenScope dropped[16];
        int dropped_count = 0;
    };
    // The innermost entry with this scope and node, removed with the entries above it (opens that never closed; looked
    // for at most 16 deep). found = false when the scope is not ours.
    Closed closed(const void* scope, const void* node);
    int depth() const { return depth_; }
    void clear() { depth_ = 0; }

private:
    OpenScope stack_[kDepth];
    int depth_ = 0;
};

// What this node's position was after our last write: the game resets it on the first touch of a frame.
struct WroteGuard {
    const void* node = nullptr;
    float x = 0, y = 0;
    bool valid = false;
};
// Adds the offset (units) to the node's position, once per reset by the game; hidden also subtracts kHideShift. A default
// edit writes nothing. True when it wrote.
bool apply_position(uint8_t* node, float dx_units, float dy_units, bool hidden, WroteGuard& guard);

struct LiveInfo {
    bool seen = false;
    uint64_t last_seen_ms = 0;
    float rect[4] = {};  // UI units, as last laid out
};
struct UiSpace {
    float w = 1920.0f, h = 1080.0f;
    bool measured = false;
};
LiveInfo hud_live(size_t element);
UiSpace hud_ui_space();
uint32_t hud_fault_code();  // the exception code of a fault in a detour, 0 for none
bool hud_bars_hooked();     // the bar detour is installed (the bars can move on their own)

// The detours' bodies, with the original passed in (tests pass a do-nothing original).
void* hud_open_path(void* scope, const NameInfo* ni, OpenFn original, uint64_t now_ms);
void hud_close_path(void* scope, CloseFn original);
void hud_set_for_tests(const SnapshotBox<HudSnapshot>* box, bool on);
void hud_reset_live_for_tests();  // live info, UI space, guards, this thread's tracker, the fault code

bool install_hud_hooks(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index,
                       const SnapshotBox<HudSnapshot>* box, std::string* why);

}  // namespace emf::core::hud
