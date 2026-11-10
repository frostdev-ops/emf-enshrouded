#pragma once
// What the game's UI hooks read: an immutable snapshot of the HUD edits, published by the editor. The detours take no
// lock and allocate nothing: they load one pointer. A writer frees an old snapshot only kGraceMs after it retired,
// far longer than a detour holds a pointer (microseconds).
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "core/hud_editor/elements.h"

namespace emf::core::hud {

struct ElementEdit {
    float x_percent = 0, y_percent = 0, scale = 1;
    bool hidden = false;
    bool outline = false;  // the game's own debug outline
};

struct HudSnapshot {
    bool active = false;  // false: the hooks change nothing
    std::array<ElementEdit, kHudElementCount> edits{};
    BarOrder order = kDefaultOrder;  // the vitals' bars, top first
    bool buffs_up = false;           // the status effects list grows upward
    uint64_t serial = 0;
};

template <class T>
class SnapshotBox {
public:
    static constexpr uint64_t kGraceMs = 2000;

    explicit SnapshotBox(T first = T{}) : current_(new T(std::move(first))) {}
    SnapshotBox(const SnapshotBox&) = delete;
    SnapshotBox& operator=(const SnapshotBox&) = delete;
    ~SnapshotBox() {
        delete current_.load(std::memory_order_relaxed);
        for (auto& r : retired_) delete r.first;
    }

    // Any thread; never null.
    const T* current() const noexcept { return current_.load(std::memory_order_acquire); }

    // Writers only, serialised by the caller. The old snapshot retires at now_ms.
    void publish(T next, uint64_t now_ms) {
        const T* fresh = new T(std::move(next));
        const T* old = current_.exchange(fresh, std::memory_order_acq_rel);
        retired_.emplace_back(old, now_ms);
        collect(now_ms);
    }

    // Frees what retired at least kGraceMs before now_ms; returns how many are still retired.
    size_t collect(uint64_t now_ms) {
        size_t keep = 0;
        for (size_t i = 0; i < retired_.size(); ++i) {
            if (now_ms >= retired_[i].second && now_ms - retired_[i].second >= kGraceMs) {
                delete retired_[i].first;
            } else {
                retired_[keep++] = retired_[i];
            }
        }
        retired_.resize(keep);
        return keep;
    }

    size_t retired() const { return retired_.size(); }

private:
    std::atomic<const T*> current_;
    std::vector<std::pair<const T*, uint64_t>> retired_;
};

}  // namespace emf::core::hud
