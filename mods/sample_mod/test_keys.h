#pragma once
// The sample mod's test keys: settings in emf\config\sample_mod.ini (key_villager, key_time, key_spawn, key_destroy,
// key_spawn2, key_give), all without a key by default, so a developer install never takes a key a shipped mod uses
// (Hand-over F7, Bulk Craft F8). They do anything only with enable_write=1.
#include <cstdint>
#include <string>

#include "emf/keys.hpp"

namespace sample {

enum TestKey { kKeyVillager, kKeyTime, kKeySpawn, kKeyDestroy, kKeySpawn2, kKeyGive, kTestKeyCount };

struct TestKeys {
    struct Key {
        const wchar_t* setting;
        const char* what;  // for the log, after the key's name: "F9 spawns spawn_guid"
        uint32_t vk = 0;   // 0 = no key
        std::string name;  // "F9"; empty without a key
    };
    Key k[kTestKeyCount] = {
        {L"key_villager", "makes the nearest villager come to you"},
        {L"key_time", "moves the time of day 3 h on"},
        {L"key_spawn", "spawns spawn_guid"},
        {L"key_destroy", "destroys the last spawn"},
        {L"key_spawn2", "spawns spawn_guid2"},
        {L"key_give", "gives give_item"},
    };
    const Key& operator[](TestKey t) const { return k[t]; }
};

// The test key of each spawn slot (slot 0 = spawn_guid, slot 1 = spawn_guid2).
inline TestKey spawn_key(int slot) { return slot == 0 ? kKeySpawn : kKeySpawn2; }

// Each key from `read(setting)`, the ini's text ("" when unset): a key name (emf::keys::parse_key), else no key.
template <class Read>
TestKeys read_test_keys(Read read) {
    TestKeys t;
    for (auto& key : t.k) {
        const std::wstring text = read(key.setting);
        key.vk = emf::keys::parse_key(text, &key.name);
        if (!key.vk) key.name.clear();
    }
    return t;
}

// The keys that are set, for the log: "F9 spawns spawn_guid, F12 gives give_item"; "" when none is.
inline std::string test_keys_text(const TestKeys& t) {
    std::string out;
    for (const auto& key : t.k)
        if (key.vk) out += (out.empty() ? "" : ", ") + key.name + " " + key.what;
    return out;
}

}  // namespace sample
