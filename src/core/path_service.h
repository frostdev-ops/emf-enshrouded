#pragma once
// path_find (API v37): mods' path queries on the game's own path grid, answered by a callback after a step or more
// (private research pathfinding_2026-11-12.md, path_query_impl_2026-11-12.md). The service keeps the queries and
// decides; EMF's internal server program calls step() with the game's calls (path_glue.cpp), tests with fakes.
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr size_t kMaxOpenPaths = 32;         // EMF's queries at once (the game's table of 1024 is shared by all walkers)
constexpr size_t kPathCreatesPerStep = 8;    // new queries sent per step
constexpr uint32_t kMaxPathPoints = 512;     // points kept for the callback
constexpr uint32_t kPathDefaultNodes = 2000, kPathMaxNodes = 20000;
constexpr float kPathMaxDistance = 200.0f;   // the game answers nothing farther apart than this, without searching
constexpr uint64_t kPathTimeoutMs = 10000;   // no answer by then (an unloaded place): ended as OTHER
constexpr float kPathReachAcross = 1.5f, kPathReachUpDown = 3.0f;  // `reaches`: the last point this near the target

// What the game says about a query: done, its PathfindingResult and the points.
struct PathAnswer {
    bool done = false;
    uint32_t game_result = 0;
    std::vector<EmfPathPoint> points;
    std::vector<uint32_t> doors;  // the game's door entity per point (empty, or one per point)
};

// The game's path query calls, made from inside EMF's program.
struct PathEngine {
    std::function<uint64_t(const EmfPathQuery& q, uint32_t owner)> create;  // the game's query id (0: refused)
    std::function<PathAnswer(uint64_t id)> poll;
    std::function<void(uint64_t id, uint32_t owner)> destroy;
    std::function<bool(uint32_t entity)> exists;
};

// A finished query, for its callback (after the step).
struct PathDone {
    uint32_t id = 0;
    EmfPathCallback cb = nullptr;
    void* user = nullptr;
    EmfPathResult result{};
    std::vector<EmfPathPoint> points;
    std::vector<uint32_t> doors;  // one per kept point, or empty when no point is a door
    // the result as a mod's callback gets it: `doors` points into this PathDone (NULL when empty)
    EmfPathResult view() const {
        EmfPathResult r = result;
        r.doors = doors.empty() ? nullptr : doors.data();
        return r;
    }
};

// The game's 0x110-byte path query (private research path_query_impl_2026-11-12.md 3.2).
constexpr size_t kPathQueryBytes = 0x110;
// Writes EMF's fields over the game's defaults (PathQueryDefaults filled `q` first): owner, start, target, targetType
// (2 = closest node as fallback), maxIterations, moveTypes (walk always), jump heights, the vertical search radii
// (4 half metres, as the game's own callers), the room the unit needs (half metres, rounded up), the time-slice flag.
void fill_path_query(uint8_t q[kPathQueryBytes], const EmfPathQuery& in, uint32_t owner);

// The game's PathfindingResult and the number of points -> EMF_PATH_*.
uint32_t path_status(uint32_t game_result, size_t points);
// One line about an answer (the console's path command).
std::string path_answer_text(uint32_t id, const EmfPathResult& r, const EmfPathPoint* points);
// "" when the query can be sent, else why not.
std::string path_query_check(const EmfPathQuery& q);

class PathService {
public:
    // Checks and queues a query (owner: an entity the game ties it to). Its id (> 0), or 0 and *why. Any thread.
    uint32_t request(const EmfPathQuery& q, uint32_t owner, EmfPathCallback cb, void* user, uint64_t now_ms,
                     std::string* why);
    // Ends a query as EMF_PATH_CANCELLED at the next step. False for an unknown or finished id. Any thread.
    bool cancel(uint32_t id);
    // Inside EMF's program on the server world: sends waiting queries, reads answers; returns the finished ones.
    std::vector<PathDone> step(const PathEngine& e, uint64_t now_ms);
    // Every query ends as cancelled, without calling the game (a new world, or the watchdog). created, when given,
    // gets the (game id, owner) of the queries the game has, for destroying them later in the same world.
    std::vector<PathDone> world_reset(std::vector<std::pair<uint64_t, uint32_t>>* created = nullptr);
    size_t open() const;

private:
    struct Query {
        uint32_t id = 0;
        EmfPathQuery q{};
        uint32_t owner = 0;
        EmfPathCallback cb = nullptr;
        void* user = nullptr;
        uint64_t asked_ms = 0;
        uint64_t game_id = 0;  // 0 until sent
        bool cancelled = false;
    };
    PathDone finish(const Query& q, uint32_t status, const std::vector<EmfPathPoint>& pts,
                    const std::vector<uint32_t>& doors = {}) const;

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Query> queries_;
    uint32_t next_id_ = 1;
};

}  // namespace emf::core
