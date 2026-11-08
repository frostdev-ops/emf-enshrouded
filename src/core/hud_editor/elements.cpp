#include "core/hud_editor/elements.h"

#include <array>
#include <cctype>

#include "common/fnv.h"

namespace emf::core::hud {

namespace {

constexpr const char* kNoteVitals = "The buffs and food row moves and scales with it; its own changes add to these.";
constexpr const char* kNoteHotbar = "The item hints and the item name above the bars move and scale with it.";
constexpr const char* kNoteCompass = "The shroud timer and meters move and scale with it; their own changes add to these.";

constexpr HudElement make(const char* id, const char* scope, const char* label, const char* note = nullptr,
                          bool confirm = false) {
    return HudElement{id, scope, label, note, fnv1a64(scope), confirm};
}

constexpr std::array<HudElement, kHudElementCount> kElements = {{
    make("vitals", "playerStatusParent", "Health and stamina", kNoteVitals, true),
    make("buffs", "buffRow", "Buffs and food"),
    make("hotbar", "doActionbar", "Hotbar", kNoteHotbar),
    make("compass", "compassSurvivalMeterParent", "Compass", kNoteCompass),
    make("shroud", "meterParent", "Shroud timer and meters"),
    make("quests", "journalInfo", "Quest tracker"),
    make("oxygen", "headDiveAndGliderParent", "Oxygen and glider"),
    make("loot", "lootLogRoot", "Loot log"),
    make("boss", "bossRoot", "Boss bar"),
    make("crosshair", "crosshairAligner", "Crosshair"),
    make("chat", "chatParent", "Chat"),
    make("notifications", "notificationParent", "Notifications"),
    make("multitool", "multiToolRoot", "Multi-tool"),
}};

// 32 slots, linear probing on hash & 31; the slot holds an element index, kUiSpaceIndex or kNoElement.
constexpr size_t kSlots = 32;
constexpr std::array<int8_t, kSlots> build_table() {
    std::array<int8_t, kSlots> t{};
    for (auto& v : t) v = static_cast<int8_t>(kNoElement);
    auto put = [&t](uint64_t h, int idx) {
        size_t s = static_cast<size_t>(h & (kSlots - 1));
        while (t[s] != static_cast<int8_t>(kNoElement)) s = (s + 1) & (kSlots - 1);
        t[s] = static_cast<int8_t>(idx);
    };
    for (size_t i = 0; i < kHudElementCount; ++i) put(kElements[i].hash, static_cast<int>(i));
    put(kUiSpaceHash, kUiSpaceIndex);
    return t;
}
constexpr std::array<int8_t, kSlots> kTable = build_table();

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool equals_nocase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

}  // namespace

std::span<const HudElement> hud_elements() { return kElements; }

int find_scope(uint64_t hash) {
    size_t s = static_cast<size_t>(hash & (kSlots - 1));
    for (size_t n = 0; n < kSlots; ++n, s = (s + 1) & (kSlots - 1)) {
        const int v = kTable[s];
        if (v == kNoElement) return kNoElement;
        if (v == kUiSpaceIndex) {
            if (hash == kUiSpaceHash) return kUiSpaceIndex;
        } else if (kElements[static_cast<size_t>(v)].hash == hash) {
            return v;
        }
    }
    return kNoElement;
}

int find_element(std::string_view id_or_scope) {
    if (id_or_scope.empty()) return kNoElement;
    for (size_t i = 0; i < kHudElementCount; ++i)
        if (equals_nocase(id_or_scope, kElements[i].id)) return static_cast<int>(i);
    for (size_t i = 0; i < kHudElementCount; ++i)
        if (equals_nocase(id_or_scope, kElements[i].scope)) return static_cast<int>(i);
    return kNoElement;
}

std::string element_ids() {
    std::string out;
    for (size_t i = 0; i < kHudElementCount; ++i) {
        if (i) out += ", ";
        out += kElements[i].id;
    }
    return out;
}

}  // namespace emf::core::hud
