// Markers (API v33): the game's map marker list (marker_map.h).
#include "core/marker_map.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/fnv.h"

namespace emf::core {

namespace {

constexpr double kFixed = 4294967296.0;  // WorldPosition: 32.32 fixed point

int64_t fixed(double v) { return static_cast<int64_t>(v * kFixed); }

bool list_ok(const MapList* l) {
    return l && l->data && l->capacity > 0 && l->capacity <= kMapCapacityMax && l->count <= l->capacity;
}

}  // namespace

void fill_map_entry(uint8_t* e, const Marker& m, const char* label, size_t len) {
    std::memset(e, 0, kMapEntryBytes);
    const int64_t pos[3] = {fixed(m.x), fixed(m.y), fixed(m.z)};
    std::memcpy(e + 0x10, pos, sizeof pos);
    std::memcpy(e + 0x30, &m.game_icon, 4);
    e[0x34] = 2;  // shown
    const uint64_t n = len;
    const uint64_t h = fnv1a64(std::string_view(label ? label : "", label ? len : 0));
    std::memcpy(e + 0x50, &label, sizeof label);
    std::memcpy(e + 0x58, &n, 8);
    std::memcpy(e + 0x60, &h, 8);
}

uint32_t emf_share(uint64_t capacity, uint64_t game_last) {
    if (capacity <= game_last + kMapGameReserve) return 0;
    return static_cast<uint32_t>(std::min<uint64_t>(kMapShareMax, capacity - game_last - kMapGameReserve));
}

void MapMarkers::pre(const MapList* list) {
    step_ = (step_ + 1) % kArenas;
    arenas_[step_].clear();  // this arena's labels were put three steps ago
    if (!list_ok(list)) return;
    game_last_ = list->count > appended_ ? list->count - appended_ : 0;
    std::lock_guard<std::mutex> lock(seen_m_);
    for (uint64_t i = appended_; i < list->count && seen_.size() < kMapSeenTypesMax; ++i) {
        const uint8_t* e = list->data + i * kMapEntryBytes;
        uint32_t entity = 0, type = 0;
        std::memcpy(&entity, e, 4);
        std::memcpy(&type, e + 0x30, 4);
        if (entity && type) seen_.insert(type);
    }
}

uint32_t MapMarkers::post(MapList* list, const std::vector<Marker>& markers, const double* player) {
    appended_ = 0;
    if (!list_ok(list)) return 0;
    std::vector<const Marker*> want;
    for (const Marker& m : markers)
        if ((m.flags & EMF_MARKER_GAME_MAP) && m.game_icon) want.push_back(&m);
    if (player) {
        auto d2 = [player](const Marker* m) {
            const double dx = m->x - player[0], dz = m->z - player[2];
            return dx * dx + dz * dz;
        };
        std::stable_sort(want.begin(), want.end(), [&](const Marker* a, const Marker* b) { return d2(a) < d2(b); });
    }
    const uint64_t room = std::min<uint64_t>(emf_share(list->capacity, game_last_), list->capacity - list->count);
    const uint64_t n = std::min<uint64_t>(room, want.size());
    auto& arena = arenas_[step_];
    for (uint64_t i = 0; i < n; ++i) {
        const Marker& m = *want[i];
        arena.push_back(m.text);  // a deque keeps every earlier string where it is
        const std::string& label = arena.back();
        fill_map_entry(list->data + (list->count + i) * kMapEntryBytes, m, label.c_str(), label.size());
    }
    list->count += n;
    appended_ = static_cast<uint32_t>(n);
    return appended_;
}

std::vector<uint32_t> MapMarkers::seen_types() const {
    std::lock_guard<std::mutex> lock(seen_m_);
    return std::vector<uint32_t>(seen_.begin(), seen_.end());
}

void MapMarkers::reset() {
    for (auto& a : arenas_) a.clear();
    step_ = 0;
    appended_ = 0;
    game_last_ = 0;
    std::lock_guard<std::mutex> lock(seen_m_);
    seen_.clear();
}

}  // namespace emf::core
