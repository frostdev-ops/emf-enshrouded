#include "core/item_text.h"

#include "common/fnv.h"
#include "common/seh.h"

namespace emf::core {

uint32_t item_name_tag(const uint8_t* info) {
    if (!info) return 0;
    uint8_t g[16];
    if (!seh_read(info + kItemInfoName, g, sizeof g)) return 0;
    bool zero = true;
    for (uint8_t b : g) zero = zero && b == 0;
    return zero ? 0 : fnv1a32(g, sizeof g);
}

void ItemNames::set_font_ready(ExtraFontReady font) {
    std::lock_guard<std::mutex> lock(mu_);
    font_ = std::move(font);
}

bool ItemNames::screen_wants_english(uint32_t language) {
    if (!language_needs_extra_font(language)) return false;
    ExtraFontReady f;
    {
        std::lock_guard<std::mutex> lock(mu_);
        f = font_;
    }
    return !(f && f(language));
}

std::optional<std::string> ItemNames::name(const uint8_t* registry, uint32_t item, NameFor f) {
    const uint32_t tag = item_name_tag(item_data_ptr(registry, item));
    if (tag == 0) return std::nullopt;
    const uint64_t key = text_.table_key();
    if (key == 0) return std::nullopt;
    const bool english = f == NameFor::Screen && screen_wants_english(text_.language());
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (key != key_) {
            cache_.clear();
            key_ = key;
        }
        const auto it = cache_.find(tag);
        if (it != cache_.end() && (!english || it->second.english_done))
            return english ? it->second.english : it->second.shown;
    }
    // the game's lookups outside the lock (they copy under SEH)
    Entry e;
    e.shown = text_.text(tag);
    if (english) {
        e.english = text_.english(tag);
        e.english_done = true;
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (key_ == key) {
        Entry& slot = cache_[tag];
        slot.shown = e.shown;
        if (e.english_done) {
            slot.english = e.english;
            slot.english_done = true;
        }
    }
    return english ? e.english : e.shown;
}

std::vector<NamedItem> ItemNames::named(const uint8_t* registry) {
    std::vector<NamedItem> out;
    for (const ItemRecord& r : list_items(registry)) {
        NamedItem n;
        n.id = r.id;
        n.name = r.name;
        if (auto s = name(registry, r.id, NameFor::Screen)) n.shown = std::move(*s);
        out.push_back(std::move(n));
    }
    return out;
}

ItemNames& item_names() {
    static ItemNames names(game_text(), [](uint32_t) { return false; });
    return names;
}

}  // namespace emf::core
