#pragma once
// The game's own text (its LocaPool: item names and the rest, in the language the game shows), for the game exe only:
// the dedicated server has no text tables (private research item_names_2026-11-10). EMF calls the game's lookup under
// SEH and copies the text at once (a language change frees the old table), checks the active table did not change
// during the call, and removes the markup. Never on the render thread.
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "common/signatures.h"

namespace emf::core {

// The game's lookup result (24 bytes): text is UTF-8, not NUL-terminated, and points into the loaded table.
struct LocaResult {
    uint8_t error;  // 0 ok, 3 not loaded yet, 0x12 tag 0, 0x1F unknown tag
    uint8_t pad[3];
    uint32_t zero;
    const char* text;
    uint64_t length;
};
struct LocaStringView {
    const char* text;
    uint64_t length;
};
using LocaLookupFn = LocaResult* (*)(LocaResult* out, void* pool, uint32_t tag);  // LocaPool_lookup
using LocaSlotFindFn = bool (*)(LocaStringView* out, void* slot, uint32_t tag);     // LocaSlot_find (one language)

// Build 1287369 (live-checked): the pool's fields.
constexpr size_t kLocaNullTag = 0x18, kLocaFallbackLanguage = 0x88, kLocaFallbackSlot = 0x90, kLocaLanguage = 0xD8,
                 kLocaActiveTable = 0x100;
constexpr uint32_t kLocaNullTagId = 0x69691905u;  // FNV-1a of 16 zero bytes, written when the game creates the pool
constexpr uint32_t kLocaMaxLanguage = 34;
constexpr uint32_t kLanguageEnUs = 1;

struct LocaAccess {
    std::function<uint8_t*()> pool;  // the LocaPool, or null (not known yet, the dedicated server)
    LocaLookupFn lookup = nullptr;
    LocaSlotFindFn slot_find = nullptr;
};

// The values the game writes when it creates the pool: the null tag's id, the fallback language En_Us, a language id
// in range. SEH-read; false for null.
bool loca_pool_sane(const uint8_t* pool);

// The game's text made fit for a line: tags <...> removed, <u+XXXX> turned into the character, spaces trimmed, at most
// 255 bytes (cut at a character boundary). Nothing for an empty result or bytes that are not UTF-8.
std::optional<std::string> clean_game_text(std::string_view raw);

// "en_us", "de_de", ... for a keen::LanguageId; "" for 0 or an id above 34.
const char* language_code(uint32_t id);
// Japanese, Korean, Thai and the two Chinese: scripts EMF's screens cannot draw without an extra font.
bool language_needs_extra_font(uint32_t id);

class GameText {
public:
    explicit GameText(LocaAccess access) : a_(std::move(access)) {}
    // The language the game shows text in (1 En_Us, 3 De_De, ...): 1 when the active language has no table (the game
    // falls back to English), 0 when there is no usable pool.
    uint32_t language() const;
    // The text of a tag in the player's language (the game's own fallbacks: English, then Keen's source text), cleaned.
    std::optional<std::string> text(uint32_t tag) const;
    // The English text of a tag: the active table while English is active, else the fallback slot (En_Us).
    std::optional<std::string> english(uint32_t tag) const;
    // Changes when the language or the active table changes (a cache keys on it); 0 = no usable pool.
    uint64_t table_key() const;

private:
    uint8_t* sane_pool() const;
    LocaAccess a_;
};

// The core's instance: empty until install_game_text found the pool's place and the lookup (the game exe of a known
// build); then the pool is read from MenuStart's gameApp each time.
const GameText& game_text();
void install_game_text(const std::array<Resolved, kSigCount>& sigs, bool known_build, int build_index);
void set_game_text_for_tests(LocaAccess access);

}  // namespace emf::core
