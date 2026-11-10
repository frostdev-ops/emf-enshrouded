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

void ItemNames::set_can_draw(ScreenCanDraw can_draw) {
    std::lock_guard<std::mutex> lock(mu_);
    can_draw_ = std::move(can_draw);
}

bool ItemNames::screen_wants_english(uint32_t language, const std::optional<std::string>& shown) {
    ScreenCanDraw f;
    {
        std::lock_guard<std::mutex> lock(mu_);
        f = can_draw_;
    }
    if (shown && f)
        if (const std::optional<bool> drawable = f(*shown)) return !*drawable;
    // not known yet: the scripts EMF's base fonts lack
    return language_needs_extra_font(language);
}

std::optional<std::string> ItemNames::name(const uint8_t* registry, uint32_t item, NameFor f) {
    const uint32_t tag = item_name_tag(item_data_ptr(registry, item));
    if (tag == 0) return std::nullopt;
    const uint64_t key = text_.table_key();
    if (key == 0) return std::nullopt;
    Entry e;
    bool cached = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (key != key_) {
            cache_.clear();
            key_ = key;
        }
        const auto it = cache_.find(tag);
        if (it != cache_.end()) {
            e = it->second;
            cached = true;
        }
    }
    // the game's lookups outside the lock (they copy under SEH)
    if (!cached) {
        e.shown = text_.text(tag);
        std::lock_guard<std::mutex> lock(mu_);
        if (key_ == key) cache_[tag].shown = e.shown;
    }
    if (f == NameFor::Game || !screen_wants_english(text_.language(), e.shown)) return e.shown;
    if (!e.english_done) {
        e.english = text_.english(tag);
        std::lock_guard<std::mutex> lock(mu_);
        if (key_ == key) {
            Entry& slot = cache_[tag];
            slot.english = e.english;
            slot.english_done = true;
        }
    }
    return e.english;
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

void pump_screen_glyphs(const uint8_t* registry, ItemNames& names, const GameText& text, ScreenGlyphs& glyphs) {
    if (!registry) return;
    const uint64_t key = text.table_key();
    if (key == 0 || key == glyphs.collected_key()) return;
    std::vector<std::string> shown;
    for (const ItemRecord& r : list_items(registry))
        if (auto s = names.name(registry, r.id, NameFor::Game)) shown.push_back(std::move(*s));
    glyphs.want(key, text.language(), extra_glyphs(shown));
}

ItemNames& item_names() {
    static ItemNames names(game_text(), [](std::string_view) -> std::optional<bool> { return std::nullopt; });
    return names;
}

}  // namespace emf::core
