#pragma once
// World settings: the fields of the game's ecs::GameSettings (144 bytes, one per world), their ranges and names, and
// reading, writing and checking a copy of the struct (docs/modding/world_settings.md). Pure: no game memory here.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace emf::core::ws {

enum class Type { Float, Bool, Enum, Minutes, Byte };

struct Field {
    const char* key;      // the struct's field name, as in enshrouded_server.json
    const char* alias;    // a short name for commands (nullptr: the key only)
    uint32_t offset;      // in the 144-byte struct
    Type type;
    double min, max;      // Float, Minutes (minutes) and Byte; Enum: 0 .. names.size() - 1; Bool: 0-1
    std::vector<const char*> names;  // Enum value names, by value
    bool needs_client;    // read by players' games too (prediction, UI): only right for them after a resync
    const char* group;
};

constexpr size_t kStructSize = 144;

std::span<const Field> fields();
// By key or alias, case-insensitive; nullptr when unknown.
const Field* find(std::string_view key_or_alias);

struct Parsed {
    bool ok = false;
    double value = 0;
    std::string error;  // names the allowed values
};
// A value as typed: numbers in the field's range (minutes for durations, whole numbers for Byte), enum names or
// numbers, 1/0/on/off/true/false/yes/no for Bool.
Parsed parse(const Field& f, std::string_view text);

double read(const uint8_t* gs, const Field& f);           // durations in minutes
void write(uint8_t* gs, const Field& f, double v);        // durations from minutes
std::string text(const Field& f, double v);               // "2 min", "NoTombstone", "on", "1.5"
std::string range_text(const Field& f);                   // "0.25 to 2", "Disabled, Rare, Normal or Often", "on or off"

// What a wrong value could crash: enums in range, bools 0 or 1, durations above 0, floats finite. The readme ranges are
// not checked here: a world's preset may lie outside them.
bool crash_safe(const uint8_t* gs, std::string* why);
// Field by field (padding ignored).
bool equal(const uint8_t* a, const uint8_t* b);
// "gameSettingsPreset": "Custom" and the "gameSettings" object, as enshrouded_server.json has them.
std::string export_block(const uint8_t* gs);

}  // namespace emf::core::ws
