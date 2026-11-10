#include "core/game_text.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "common/seh.h"
#include "core/menu_start.h"

namespace emf::core {

namespace {

const char* const kCodes[kLocaMaxLanguage + 1] = {
    "",      "en_us", "en_gb", "de_de", "nl_nl", "fr_fr", "it_it", "es_es", "es_mx", "ru_ru", "pl_pl", "ja_jp",
    "id_id", "ko_kr", "ms_my", "pt_pt", "pt_br", "th_th", "tr_tr", "vi_vn", "zh_cn", "zh_tw", "fr_ca", "es_419",
    "bg_bg", "cs_cz", "da_dk", "fi_fi", "el_gr", "hu_hu", "no_no", "nb_no", "ro_ro", "sv_se", "uk_ua"};

constexpr size_t kMaxRawText = 1024;
constexpr size_t kMaxCleanText = 255;

// ---- guarded calls into the game (plain data only: MSVC C2712) ----
bool guarded_lookup(LocaLookupFn fn, void* pool, uint32_t tag, LocaResult* out) {
    __try {
        fn(out, pool, tag);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_slot_find(LocaSlotFindFn fn, void* slot, uint32_t tag, LocaStringView* out, bool* found) {
    __try {
        *found = fn(out, slot, tag);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guarded_copy(const char* src, uint64_t length, char* dst, size_t cap, size_t* n) {
    const size_t take = length < cap ? static_cast<size_t>(length) : cap;
    __try {
        std::memcpy(dst, src, take);
        *n = take;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *n = 0;
        return false;
    }
}

uint64_t read_u64(const uint8_t* p) {
    uint64_t v = 0;
    return seh_read(p, &v, sizeof v) ? v : 0;
}
uint32_t read_u32(const uint8_t* p) {
    uint32_t v = 0;
    return seh_read(p, &v, sizeof v) ? v : 0;
}

// Valid UTF-8 (no overlong forms, no surrogates, at most U+10FFFF).
bool valid_utf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        const size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) return false;
        uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t k = 1; k < len; ++k) {
            const auto d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3Fu);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// "u+00d7" (any case, 1 to 6 hex digits) -> the code point; 0 when it is not one.
uint32_t code_point_tag(std::string_view tag) {
    if (tag.size() < 3 || tag.size() > 8 || (tag[0] != 'u' && tag[0] != 'U') || tag[1] != '+') return 0;
    uint32_t cp = 0;
    for (size_t i = 2; i < tag.size(); ++i) {
        const char c = tag[i];
        const int v = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (v < 0) return 0;
        cp = cp * 16 + static_cast<uint32_t>(v);
    }
    return cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ? 0 : cp;
}

}  // namespace

bool loca_pool_sane(const uint8_t* pool) {
    if (!pool) return false;
    uint32_t null_tag = 0, fallback = 0, language = 0;
    if (!seh_read(pool + kLocaNullTag, &null_tag, 4) || !seh_read(pool + kLocaFallbackLanguage, &fallback, 4) ||
        !seh_read(pool + kLocaLanguage, &language, 4))
        return false;
    return null_tag == kLocaNullTagId && fallback == kLanguageEnUs && language <= kLocaMaxLanguage;
}

std::optional<std::string> clean_game_text(std::string_view raw) {
    if (!valid_utf8(raw)) return std::nullopt;
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '<') {
            const size_t end = raw.find('>', i + 1);
            if (end == std::string_view::npos) {  // an unterminated '<' is text
                out.append(raw.substr(i));
                break;
            }
            if (const uint32_t cp = code_point_tag(raw.substr(i + 1, end - i - 1))) append_utf8(out, cp);
            i = end + 1;
            continue;
        }
        out += raw[i++];
    }
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0, e = out.size();
    while (b < e && space(out[b])) ++b;
    while (e > b && space(out[e - 1])) --e;
    out = out.substr(b, e - b);
    if (out.size() > kMaxCleanText) {
        size_t cut = kMaxCleanText;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
        out.resize(cut);
    }
    if (out.empty()) return std::nullopt;
    return out;
}

const char* language_code(uint32_t id) { return id <= kLocaMaxLanguage ? kCodes[id] : ""; }

bool language_needs_extra_font(uint32_t id) { return id == 11 || id == 13 || id == 17 || id == 20 || id == 21; }

uint8_t* GameText::sane_pool() const {
    if (!a_.pool) return nullptr;
    uint8_t* p = a_.pool();
    return loca_pool_sane(p) ? p : nullptr;
}

uint32_t GameText::language() const {
    const uint8_t* p = sane_pool();
    if (!p) return 0;
    if (read_u64(p + kLocaActiveTable) == 0) return kLanguageEnUs;  // no table for it: the game shows English
    const uint32_t lang = read_u32(p + kLocaLanguage);
    return lang ? lang : kLanguageEnUs;
}

uint64_t GameText::table_key() const {
    const uint8_t* p = sane_pool();
    if (!p) return 0;
    return (static_cast<uint64_t>(read_u32(p + kLocaLanguage)) << 48) ^ read_u64(p + kLocaActiveTable) ^ 1;
}

std::optional<std::string> GameText::text(uint32_t tag) const {
    if (tag == 0 || !a_.lookup) return std::nullopt;
    uint8_t* p = sane_pool();
    if (!p) return std::nullopt;
    const uint64_t before = read_u64(p + kLocaActiveTable);
    LocaResult r{};
    if (!guarded_lookup(a_.lookup, p, tag, &r) || r.error != 0 || !r.text || r.length == 0) return std::nullopt;
    char buf[kMaxRawText];
    size_t n = 0;
    if (!guarded_copy(r.text, r.length, buf, sizeof buf, &n)) return std::nullopt;
    if (read_u64(p + kLocaActiveTable) != before) return std::nullopt;  // a language change finished meanwhile
    return clean_game_text(std::string_view(buf, n));
}

std::optional<std::string> GameText::english(uint32_t tag) const {
    if (tag == 0) return std::nullopt;
    uint8_t* p = sane_pool();
    if (!p) return std::nullopt;
    if (read_u32(p + kLocaLanguage) == kLanguageEnUs) return text(tag);  // English is the active table
    if (!a_.slot_find) return std::nullopt;
    const uint64_t before = read_u64(p + kLocaFallbackSlot + 0x20);
    LocaStringView v{};
    bool found = false;
    if (!guarded_slot_find(a_.slot_find, p + kLocaFallbackSlot, tag, &v, &found) || !found || !v.text || v.length == 0)
        return std::nullopt;
    char buf[kMaxRawText];
    size_t n = 0;
    if (!guarded_copy(v.text, v.length, buf, sizeof buf, &n)) return std::nullopt;
    if (read_u64(p + kLocaFallbackSlot + 0x20) != before) return std::nullopt;
    return clean_game_text(std::string_view(buf, n));
}

namespace {
GameText g_text{LocaAccess{}};
}  // namespace

const GameText& game_text() { return g_text; }

void set_game_text_for_tests(LocaAccess access) { g_text = GameText(std::move(access)); }

namespace {
std::atomic<uintptr_t> g_app{0};
}  // namespace

void note_game_app(uintptr_t app) { g_app.store(app, std::memory_order_release); }
uintptr_t noted_game_app() { return g_app.load(std::memory_order_acquire); }

bool install_game_text(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index, std::string* why) {
    const Resolved& l = sigs[static_cast<size_t>(Sig::LocaPoolLookup)];
    const auto layout = menu_app_layout(build_index);
    if (!is_usable(l, known_build)) {
        if (why) *why = "the LocaPoolLookup signature is not usable on this game build";
        return false;
    }
    if (!layout || layout->loca_pool == 0) {
        if (why) *why = "EMF does not know where this game build keeps its text pool";
        return false;
    }
    LocaAccess a;
    a.lookup = reinterpret_cast<LocaLookupFn>(l.addr);
    const Resolved& f = sigs[static_cast<size_t>(Sig::LocaSlotFind)];
    if (is_usable(f, known_build)) a.slot_find = reinterpret_cast<LocaSlotFindFn>(f.addr);
    const uint32_t off = layout->loca_pool;
    a.pool = [off]() -> uint8_t* {
        uintptr_t app = noted_game_app();  // the pregame hook (every player of the game exe)
        if (!app) {
            MenuStart* m = menu_start();  // the debug bridge's menu hooks, when they are on
            app = m ? m->game_app() : 0;
        }
        if (!app) return nullptr;
        uint8_t* pool = nullptr;
        return seh_read(reinterpret_cast<const void*>(app + off), &pool, sizeof pool) ? pool : nullptr;
    };
    g_text = GameText(std::move(a));
    return true;
}

}  // namespace emf::core
