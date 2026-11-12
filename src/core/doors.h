#pragma once
// Doors (API v38): open, close and lock doors and gates from the server, through the internal server program
// "emf_doors" and the game's own writeOther (private research doors_2026-11-12.md: the state is
// ServerToggleState.state, the swing DirectionalInteraction.state; a lock is InteractionOfferRuntime.isDisabled, held
// each step since the game does not save it).
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "common/world_view.h"
#include "core/program_access.h"
#include "core/system_programs.h"
#include "emf/emf_api.h"

namespace emf::core {

constexpr const char* kDoorsProgramName = "emf_doors";
constexpr size_t kMaxDoorOrders = 256;   // waiting for the next step
constexpr size_t kMaxLockedDoors = 1024;

enum class DoorOrderKind : uint8_t { Open, Close, Lock, Unlock };

class DoorService {
public:
    struct Deps {
        ReadOtherFn read_other = nullptr;
        WriteOtherFn write_other = nullptr;
        GetComponentFn get_component = nullptr;
        std::function<void(const std::string&)> log;
    };
    explicit DoorService(Deps deps);
    ProgramSpec spec();
    // EMF_DOOR_NONE / _CLOSED / _OPEN, with EMF_DOOR_LOCKED and EMF_DOOR_LOGIC; *side = the swing (0 closed, 1 front,
    // 2 back). Inside a world callback on `world` (it reads components).
    uint32_t state(uint8_t* world, uint32_t entity, uint8_t* side = nullptr) const;
    // Queues an order for the next server step (side: 0 = back, the game's default; 1 front; 2 back). False and *why:
    // not the server world, not a door, driven by a switch, already so, a bad side, too many waiting.
    bool request(uint8_t* world, uint32_t entity, DoorOrderKind kind, uint8_t side, std::string* why);
    // The program callback.
    void run(void* program_context);
    // A new server world: orders and locks of the old one are dropped.
    void world_reset();
    size_t locked_count() const;

private:
    struct Order {
        uint8_t* world = nullptr;
        uint32_t entity = 0;
        DoorOrderKind kind = DoorOrderKind::Open;
        uint8_t side = 0;
    };
    bool is_locked(uint32_t entity) const;
    void say(const std::string& line);

    Deps deps_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Order> orders_;
    std::vector<uint32_t> locked_;
    bool said_ = false;
};

}  // namespace emf::core
