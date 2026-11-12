// path_find (path_service.h).
#include "core/path_service.h"

#include "core/api_sizes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace emf::core {

namespace {
constexpr float kWorldLimit = 100000.0f;  // metres: anything beyond is a bad number

bool finite3(const float v[3]) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(v[i]) || std::fabs(v[i]) > kWorldLimit) return false;
    return true;
}
float dist(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
constexpr uint16_t kKnownMoves = EMF_PATH_WALK | EMF_PATH_JUMP_UP | EMF_PATH_JUMP_DOWN | EMF_PATH_JUMP_ACROSS |
                                 EMF_PATH_OPEN_DOOR | EMF_PATH_SWIM;
}  // namespace

void fill_path_query(uint8_t q[kPathQueryBytes], const EmfPathQuery& in, uint32_t owner) {
    auto put = [&](size_t off, const void* v, size_t n) { std::memcpy(q + off, v, n); };
    auto put_u32 = [&](size_t off, uint32_t v) { put(off, &v, 4); };
    put_u32(0x00, owner);
    put(0x10, in.from, 12);
    put(0x1c, in.to, 12);
    q[0x36] = in.closest ? 2 : 0;
    put_u32(0x48, in.max_nodes ? in.max_nodes : kPathDefaultNodes);
    const uint16_t moves = static_cast<uint16_t>(in.moves | EMF_PATH_WALK);
    put(0x54, &moves, 2);
    q[0x56] = in.max_jump_up;
    q[0x57] = in.max_jump_down;
    q[0xca] = 1;
    put_u32(0xf4, 4);
    put_u32(0xfc, 4);
    put_u32(0x100, in.width > 0 ? static_cast<uint32_t>(std::ceil(in.width * 2.0f)) : 0u);
    put_u32(0x104, in.height > 0 ? static_cast<uint32_t>(std::ceil(in.height * 2.0f)) : 0u);
}

std::string path_answer_text(uint32_t id, const EmfPathResult& r, const EmfPathPoint* points) {
    const std::string head = "path " + std::to_string(id) + ": ";
    switch (r.status) {
        case EMF_PATH_FOUND: break;
        case EMF_PATH_NO_PATH: return head + "no path";
        case EMF_PATH_TOO_LONG: return head + "too long (the search ran out of nodes)";
        case EMF_PATH_NO_START: return head + "no path grid at the start (not loaded, or inside something)";
        case EMF_PATH_NO_TARGET: return head + "no path grid at the target (not loaded, or inside something)";
        case EMF_PATH_NO_MEMORY: return head + "the search ran out of memory";
        case EMF_PATH_CANCELLED: return head + "cancelled";
        default: return head + "no answer from the game";
    }
    uint32_t jumps = 0, doors = 0;
    for (uint32_t i = 0; points && i < r.count; ++i) {
        if (points[i].move >= 2 && points[i].move <= 4) ++jumps;
        if (points[i].move == 5) ++doors;
    }
    char buf[256];
    const EmfPathPoint* last = points && r.count ? &points[r.count - 1] : nullptr;
    std::snprintf(buf, sizeof buf, "found, %s, %u points, %.1f m, ends at (%.1f, %.1f, %.1f); %u jump%s, %u door%s",
                  r.reaches ? "reaches the target" : "stops short of the target", r.count, r.length,
                  last ? last->x : 0.0f, last ? last->y : 0.0f, last ? last->z : 0.0f, jumps, jumps == 1 ? "" : "s",
                  doors, doors == 1 ? "" : "s");
    return head + buf;
}

uint32_t path_status(uint32_t game_result, size_t points) {
    switch (game_result) {
        case 1: return points ? EMF_PATH_FOUND : EMF_PATH_NO_PATH;  // PathFound (also the closest-node fallback)
        case 2:                                                      // NoPathFound
        case 3:                                                      // NoFreePath
        case 4: return EMF_PATH_NO_PATH;                             // NoOccupiedPath
        case 5: return EMF_PATH_NO_START;
        case 6: return EMF_PATH_NO_TARGET;
        case 7: return EMF_PATH_TOO_LONG;                            // MaxIterationsReached
        case 8:                                                      // OpenListOutOfMemory
        case 9: return EMF_PATH_NO_MEMORY;                           // NodesOutOfMemory
        default: return EMF_PATH_OTHER;
    }
}

std::string path_query_check(const EmfPathQuery& q) {
    if (q.size < kPathQueryV37Size) return "the query's size field is not sizeof(EmfPathQuery)";
    if (!finite3(q.from) || !finite3(q.to) || !std::isfinite(q.width) || !std::isfinite(q.height) || q.width < 0 ||
        q.height < 0 || q.width > 64 || q.height > 64)
        return "bad numbers (positions in metres, width and height 0-64 m)";
    if (dist(q.from, q.to) > kPathMaxDistance) return "from and to are more than 200 m apart (the game does not search that far)";
    if (q.moves & ~kKnownMoves) return "unknown move bits (EMF_PATH_*)";
    if (q.max_nodes > kPathMaxNodes) return "max_nodes over 20000";
    return {};
}

uint32_t PathService::request(const EmfPathQuery& q, uint32_t owner, EmfPathCallback cb, void* user, uint64_t now_ms,
                              std::string* why) {
    std::string w = path_query_check(q);
    if (w.empty() && !owner) w = "no owner entity (the game ties a query to one; pass owner, or have a player in the world)";
    if (!w.empty()) {
        if (why) *why = w;
        return 0;
    }
    AcquireSRWLockExclusive(&lock_);
    if (queries_.size() >= kMaxOpenPaths) {
        ReleaseSRWLockExclusive(&lock_);
        if (why) *why = "32 path queries are open already (wait for answers)";
        return 0;
    }
    Query x;
    x.id = next_id_++;
    if (!next_id_) next_id_ = 1;
    x.q = q;
    x.owner = owner;
    x.cb = cb;
    x.user = user;
    x.asked_ms = now_ms;
    queries_.push_back(x);
    ReleaseSRWLockExclusive(&lock_);
    return x.id;
}

bool PathService::cancel(uint32_t id) {
    AcquireSRWLockExclusive(&lock_);
    bool found = false;
    for (Query& x : queries_)
        if (x.id == id && !x.cancelled) x.cancelled = found = true;
    ReleaseSRWLockExclusive(&lock_);
    return found;
}

PathDone PathService::finish(const Query& x, uint32_t status, const std::vector<EmfPathPoint>& pts) const {
    PathDone d;
    d.id = x.id;
    d.cb = x.cb;
    d.user = x.user;
    d.result.size = sizeof(EmfPathResult);
    d.result.status = status;
    if (status == EMF_PATH_FOUND && !pts.empty()) {
        const EmfPathPoint& last = pts.back();  // the game's last point, also when fewer are kept
        const float dx = last.x - x.q.to[0], dz = last.z - x.q.to[2], dy = last.y - x.q.to[1];
        d.result.reaches = std::sqrt(dx * dx + dz * dz) <= kPathReachAcross && std::fabs(dy) <= kPathReachUpDown;
    }
    const size_t keep = (std::min)(pts.size(), size_t{kMaxPathPoints});
    d.points.assign(pts.begin(), pts.begin() + keep);
    d.result.count = static_cast<uint32_t>(keep);
    float len = 0;
    float at[3] = {x.q.from[0], x.q.from[1], x.q.from[2]};
    for (const EmfPathPoint& p : d.points) {
        const float to[3] = {p.x, p.y, p.z};
        len += dist(at, to);
        at[0] = p.x;
        at[1] = p.y;
        at[2] = p.z;
    }
    d.result.length = len;
    return d;
}

std::vector<PathDone> PathService::step(const PathEngine& e, uint64_t now_ms) {
    std::vector<PathDone> done;
    AcquireSRWLockExclusive(&lock_);
    std::vector<Query> work = queries_;  // the engine is called without the lock (cancel may come from any thread)
    ReleaseSRWLockExclusive(&lock_);
    std::vector<std::pair<uint32_t, uint64_t>> sent;  // id -> game id
    std::vector<uint32_t> ended;
    size_t creates = 0;
    for (Query& x : work) {
        auto end = [&](uint32_t status, const std::vector<EmfPathPoint>& pts) {
            if (x.game_id && e.destroy) e.destroy(x.game_id, x.owner);  // the game never drops a query by itself
            done.push_back(finish(x, status, pts));
            ended.push_back(x.id);
        };
        if (x.cancelled || (e.exists && !e.exists(x.owner))) {
            end(EMF_PATH_CANCELLED, {});
            continue;
        }
        if (!x.game_id) {
            if (creates >= kPathCreatesPerStep || !e.create) continue;
            ++creates;
            x.game_id = e.create(x.q, x.owner);
            if (!x.game_id) {
                end(EMF_PATH_OTHER, {});  // the game's command buffer was full
                continue;
            }
            sent.push_back({x.id, x.game_id});
            continue;  // the record exists after this step's flush: the answer comes later
        }
        const PathAnswer a = e.poll ? e.poll(x.game_id) : PathAnswer{};
        if (a.done) {
            end(path_status(a.game_result, a.points.size()), a.points);
        } else if (now_ms - x.asked_ms > kPathTimeoutMs) {
            end(EMF_PATH_OTHER, {});
        }
    }
    AcquireSRWLockExclusive(&lock_);
    for (PathDone& d : done)  // cancelled while the step talked to the game: the answer says so
        for (const Query& x : queries_)
            if (x.id == d.id && x.cancelled && d.result.status != EMF_PATH_CANCELLED) d = finish(x, EMF_PATH_CANCELLED, {});
    for (const auto& [id, gid] : sent)
        for (Query& x : queries_)
            if (x.id == id) x.game_id = gid;
    queries_.erase(std::remove_if(queries_.begin(), queries_.end(),
                                  [&](const Query& x) {
                                      return std::find(ended.begin(), ended.end(), x.id) != ended.end();
                                  }),
                   queries_.end());
    ReleaseSRWLockExclusive(&lock_);
    return done;
}

std::vector<PathDone> PathService::world_reset() {
    AcquireSRWLockExclusive(&lock_);
    std::vector<Query> all;
    all.swap(queries_);
    ReleaseSRWLockExclusive(&lock_);
    std::vector<PathDone> done;
    for (const Query& x : all) done.push_back(finish(x, EMF_PATH_CANCELLED, {}));
    return done;
}

size_t PathService::open() const {
    AcquireSRWLockShared(&lock_);
    const size_t n = queries_.size();
    ReleaseSRWLockShared(&lock_);
    return n;
}

}  // namespace emf::core
