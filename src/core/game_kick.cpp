#include "game_kick.h"

#include <windows.h>

#include <cstring>

#include "server_chat.h"

namespace emf::core {
namespace {
// No C++ objects here: __try cannot share a frame with them.
bool call_guarded(HandleAccountActionFn fn, uint8_t* server, const uint8_t* ev) {
    __try {
        fn(server, ev);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

std::array<uint8_t, kAccountActionEventSize> kick_event(uint64_t account) {
    std::array<uint8_t, kAccountActionEventSize> ev{};
    std::memcpy(ev.data() + 8, &account, sizeof account);
    ev[0x14] = kAccountActionKick;
    return ev;
}

bool kick_account(HandleAccountActionFn fn, uint8_t* game_server, uint64_t account) {
    if (!fn || !game_server || !account) return false;
    const auto ev = kick_event(account);
    return call_guarded(fn, game_server, ev.data());
}

std::optional<bool> connection_is_admin(const uint8_t* records, uint64_t account) {
    using namespace server_layout;
    if (!records || !account) return std::nullopt;
    for (uint32_t i = 0; i < kConnectionCount; ++i) {
        const uint8_t* r = records + static_cast<size_t>(i) * kConnectionSize;
        uint32_t handle = 0;
        uint64_t acct = 0;
        std::memcpy(&handle, r, sizeof handle);
        std::memcpy(&acct, r + kConnectionAccount, sizeof acct);
        if (handle == 0 || (handle & 0x3F) != i || acct != account) continue;
        return (r[kConnectionPerms] & 1) != 0;
    }
    return std::nullopt;
}

}  // namespace emf::core
