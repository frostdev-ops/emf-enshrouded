#pragma once
// Doors (API v38): open, close and lock doors and gates from the server, through the internal server program
// "emf_doors" and the game's own writeOther (private research doors_2026-11-12.md: the state is
// ServerToggleState.state, the swing DirectionalInteraction.state; a lock is InteractionOfferRuntime.isDisabled, held
// each step since the game does not save it). Locks are kept per world (Deps::save, load_saved) by the door's place:
// entity ids change between loads; after a load the door at each saved place is looked for and locked again.
#include <windows.h>

#include <cstdint>
#include <array>
#include <functional>
#include <set>
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
constexpr uint64_t kLockLookMs = 3000;    // saved places whose door is not loaded yet are looked for this often,
constexpr uint64_t kLockLookSlowMs = 30000;  // and after kLockLooksFast looks this often (a door that was broken)
constexpr uint32_t kLockLooksFast = 20;

enum class DoorOrderKind : uint8_t { Open, Close, Lock, Unlock };

class DoorService {
public:
    struct Deps {
        ReadOtherFn read_other = nullptr;
        WriteOtherFn write_other = nullptr;
        GetComponentFn get_component = nullptr;
        std::function<void(const std::string&)> log;
        // the world's file of locks: false when it cannot be written now (tried again at the next tick)
        std::function<bool(const std::string&)> save;
        // the door standing at a place (within half a metre), 0 when none is loaded there
        std::function<uint32_t(uint8_t* world, const double* pos)> door_at;
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
    // The world's saved locks (its file, read once after its save loaded): each door at a saved place is locked again.
    void load_saved(const std::string& text);
    // After a server step (no world jobs running): the locks are saved when they changed, and saved places whose door
    // was not found yet are looked for (every kLockLookMs).
    void server_tick(uint8_t* world, uint64_t now_ms);
    std::string saved_text() const;
    size_t locked_count() const;

private:
    struct Order {
        uint8_t* world = nullptr;
        uint32_t entity = 0;
        DoorOrderKind kind = DoorOrderKind::Open;
        uint8_t side = 0;
    };
    struct Locked {
        uint32_t entity = 0;
        int8_t held = -1;  // the state it holds: -1 not read yet, 0 closed, 1 open
        uint8_t side = 0;  // the swing it holds
        bool has_pos = false;  // its place (for the world's file)
        std::array<double, 3> pos{};
    };
    Locked* find_locked(uint32_t entity);  // under lock_
    bool is_locked(uint32_t entity) const;
    void say(const std::string& line);

    Deps deps_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Order> orders_;
    std::vector<Locked> locked_;
    std::vector<std::array<double, 3>> pending_;  // saved places whose door was not found yet
    uint64_t next_look_ = 0;
    uint32_t looks_ = 0;
    bool dirty_ = false;  // the locks changed since the file was written
    std::set<std::string> said_;  // problem lines logged in this world
};

}  // namespace emf::core
