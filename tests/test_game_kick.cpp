// Refusing a join: the game's kick (handleAccountAction with a Kick event) and the connection's admin bit
// (src/core/game_kick.h).
#include <doctest.h>

#include <cstring>
#include <vector>

#include "core/game_kick.h"
#include "core/server_chat.h"

namespace {
uint8_t* g_seen_server = nullptr;
uint8_t g_seen_event[24];
int g_calls = 0;
void __fastcall fake_handler(uint8_t* server, const uint8_t* ev) {
    g_seen_server = server;
    std::memcpy(g_seen_event, ev, sizeof g_seen_event);
    ++g_calls;
}
void __fastcall faulting_handler(uint8_t*, const uint8_t*) {
    volatile int* p = nullptr;
    *p = 1;
}
}  // namespace

TEST_CASE("game kick: the event is the game's 24-byte Kick for the account") {
    const auto ev = emf::core::kick_event(0x1122334455667788ull);
    REQUIRE(ev.size() == 24);
    uint64_t account = 0;
    std::memcpy(&account, ev.data() + 8, 8);
    CHECK(account == 0x1122334455667788ull);
    uint32_t group = 1;
    std::memcpy(&group, ev.data() + 0x10, 4);
    CHECK(group == 0);
    CHECK(ev[0x14] == 0);  // Kick; never Ban (1) or UnBan (2)
    for (size_t i = 0; i < 8; ++i) CHECK(ev[i] == 0);
}

TEST_CASE("game kick: the handler gets the server and the event; nothing without a server; a fault is false") {
    uint8_t server[64] = {};
    g_calls = 0;
    CHECK(emf::core::kick_account(&fake_handler, server, 0xABCDull));
    CHECK(g_calls == 1);
    CHECK(g_seen_server == server);
    uint64_t account = 0;
    std::memcpy(&account, g_seen_event + 8, 8);
    CHECK(account == 0xABCDull);
    CHECK(g_seen_event[0x14] == 0);
    CHECK_FALSE(emf::core::kick_account(&fake_handler, nullptr, 0xABCDull));
    CHECK_FALSE(emf::core::kick_account(&fake_handler, server, 0));
    CHECK_FALSE(emf::core::kick_account(nullptr, server, 0xABCDull));
    CHECK(g_calls == 1);
    CHECK_FALSE(emf::core::kick_account(&faulting_handler, server, 0xABCDull));
}

TEST_CASE("game kick: the admin bit is read from the record that holds that connection only") {
    using namespace emf::core::server_layout;
    std::vector<uint8_t> records(kConnectionSize * kConnectionCount, 0);
    const auto put = [&](uint32_t i, uint32_t handle, uint64_t account, uint8_t perms) {
        uint8_t* r = records.data() + i * kConnectionSize;
        std::memcpy(r, &handle, 4);
        std::memcpy(r + kConnectionAccount, &account, 8);
        r[kConnectionPerms] = perms;
    };
    put(1, 0x41, 0xAA, 0x00);  // a player
    put(2, 0x42, 0xBB, 0x3F);  // an admin (CanKickBan and the rest)
    put(3, 0x45, 0xCC, 0x01);  // a stale record: its handle names record 5, not 3
    CHECK(emf::core::connection_is_admin(records.data(), 0xAA) == std::optional<bool>(false));
    CHECK(emf::core::connection_is_admin(records.data(), 0xBB) == std::optional<bool>(true));
    CHECK_FALSE(emf::core::connection_is_admin(records.data(), 0xCC).has_value());
    CHECK_FALSE(emf::core::connection_is_admin(records.data(), 0xDD).has_value());
    CHECK_FALSE(emf::core::connection_is_admin(records.data(), 0).has_value());
}
