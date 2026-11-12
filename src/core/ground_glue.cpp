// Markers step 2 (ground_glue.h).
#include "core/ground_glue.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "common/seh.h"
#include "common/world_view.h"
#include "core/api.h"

namespace emf::core {

namespace {

constexpr double kFixed = 4294967296.0;
std::atomic<VoxelRayFn> g_ray{nullptr};
std::atomic<FogSampleFn> g_fog{nullptr};
std::string g_fog_desc;  // g_desc_m
std::atomic<uint32_t> g_realm{0};
std::atomic<bool> g_box_known{false};
std::atomic<double> g_box_top{0};
using SetterFn = void* (*)(void*, void*, void*, void*);
SetterFn g_o_setter = nullptr;
std::mutex g_desc_m;
std::string g_desc;

void read_box(const uint8_t* manager) {
    const uint8_t* data = nullptr;
    uint64_t count = 0;
    int64_t top = 0;
    // box 0: worldMin (24 B) then worldMax (24 B); its y is at +0x20
    if (!seh_read(manager + 0xb0, &data, 8) || !seh_read(manager + 0xb8, &count, 8) || !data || count == 0 ||
        count > 1024 || !seh_read(data + 0x20, &top, 8))
        return;
    const double y = static_cast<double>(top) / kFixed;
    if (!std::isfinite(y) || y < -10000 || y > 100000) return;
    g_box_top.store(y, std::memory_order_relaxed);
    g_box_known.store(true, std::memory_order_release);
}

void* hk_setter(void* manager, void* src, void* a, void* b) {
    void* r = g_o_setter(manager, src, a, b);
    read_box(static_cast<const uint8_t*>(manager));
    return r;
}

struct Grid {
    void* terrain = nullptr;
    uint8_t realm = 0;
    double ox = 0, oy = 0, oz = 0, size = 0;
    uint32_t nx = 0, ny = 0;
};

bool read_grid(EmfWorld* world, Grid* g, std::string* why) {
    auto fail = [&](const char* m) {
        if (why) *why = m;
        return false;
    };
    if (!world) return fail("no world");
    auto* terrain = static_cast<uint8_t*>(WorldView(reinterpret_cast<uint8_t*>(world)).singleton("ecs::VoxelGrid"));
    if (!terrain) return fail("the world's terrain is not there");
    uint8_t* realms = nullptr;
    uint64_t count = 0;
    if (!seh_read(terrain + 0x1bf8, &realms, 8) || !seh_read(terrain + 0x1c00, &count, 8) || !realms || !count ||
        count > 256)
        return fail("the terrain's realm table cannot be read");
    const uint32_t realm = g_realm.load(std::memory_order_relaxed);
    if (realm >= count) return fail("the realm is not in the terrain's table");
    uint8_t* grid = nullptr;
    if (!seh_read(realms + static_cast<size_t>(realm) * 0x3870, &grid, 8) || !grid) return fail("the realm has no grid yet");
    uint32_t shift = 0, nx = 0, ny = 0;
    float o[3];
    if (!seh_read(grid + 0x10, &shift, 4) || !seh_read(grid + 0x14, &nx, 4) || !seh_read(grid + 0x18, &ny, 4) ||
        !seh_read(grid + 0x70, o, sizeof o))
        return fail("the grid cannot be read");
    if (shift > 12 || !nx || !ny || nx > 65536 || ny > 4096 || !std::isfinite(o[0]) || !std::isfinite(o[1]) ||
        !std::isfinite(o[2]))
        return fail("the grid's numbers are not sane");
    g->terrain = terrain;
    g->realm = static_cast<uint8_t>(realm);
    g->size = static_cast<double>(uint64_t(1) << shift);
    g->nx = nx;
    g->ny = ny;
    g->ox = o[0];
    g->oy = o[1];
    g->oz = o[2];
    if (g->ny * g->size > 32768) return fail("the grid is taller than expected");
    return true;
}

bool call_ray(VoxelRayFn fn, void* hit, void* terrain, const float* from, const float* to, const uint8_t* options) {
    __try {
        fn(hit, terrain, from, to, options);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_fog(FogSampleFn fn, void* out, const void* fq, const int64_t* pos) {
    __try {
        fn(out, fq, pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void ground_set_fog(FogSampleFn fn) { g_fog.store(fn, std::memory_order_release); }

bool ground_fog(EmfWorld* world, double x, double y, double z, FogReading* out) {
    FogSampleFn fn = g_fog.load(std::memory_order_acquire);
    if (!fn || !world || !out || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    WorldView v(reinterpret_cast<uint8_t*>(world));
    const auto* fq = static_cast<const uint8_t*>(v.singleton("FogQuery"));
    const auto* mapping = static_cast<const uint8_t*>(v.singleton("FogVoxelMapping"));
    const auto* sps = static_cast<const uint8_t*>(v.singleton("ecs::ServerProgressSystem"));
    const uint8_t* fw = nullptr;
    const uint8_t* fq_mapping = nullptr;
    const uint8_t* handles = nullptr;
    uint32_t res = 0, tile_x = 0, tile_z = 0, count_x = 0, count_z = 0, progress = 0;
    if (!fq || !mapping || !sps || !seh_read(fq + 8, &fw, 8) || !fw || !seh_read(fq + 0x10, &fq_mapping, 8) ||
        fq_mapping != mapping || !seh_read(fw + 0x14, &res, 4) || !res || !seh_read(fw + 0x108, &tile_x, 4) ||
        !seh_read(fw + 0x10C, &tile_z, 4) || !tile_x || !tile_z || !seh_read(fw + 0x110, &count_x, 4) ||
        !seh_read(fw + 0x114, &count_z, 4) || !count_x || !count_z || !seh_read(fw + 0x118, &handles, 8) || !handles ||
        !seh_read(sps + 0x14, &progress, 4)) {
        // once per server world: the map teleport's shroud check is off here (the fog stays unknown)
        static std::atomic<EmfWorld*> told{nullptr};
        if (told.exchange(world) != world) {
            CoreState* s = core_state();
            if (s && s->log_line)
                s->log_line("map: the shroud cannot be read in this world (ground_at answers the fog unknown; the map "
                            "teleport's shroud check is off here)");
        }
        return false;
    }
    const int64_t pos[3] = {std::llround(x * kFixed), std::llround(y * kFixed), std::llround(z * kFixed)};
    alignas(16) uint8_t buf[0x50] = {};
    if (!call_fog(fn, buf, fq, pos)) return false;
    out->loaded = buf[0] == 0;
    out->progress = progress;
    std::memcpy(out->w, buf + 8, sizeof out->w);
    std::lock_guard<std::mutex> lock(g_desc_m);
    if (g_fog_desc.empty()) {
        char line[200];
        std::snprintf(line, sizeof line, "fog: progress level %u, fog world tiles %ux%u of %ux%u, resource %u", progress,
                      count_x, count_z, tile_x, tile_z, res);
        g_fog_desc = line;
    }
    return true;
}

std::string ground_fog_describe() {
    std::lock_guard<std::mutex> lock(g_desc_m);
    return g_fog_desc;
}

void ground_set_ray(VoxelRayFn fn) { g_ray.store(fn, std::memory_order_release); }
bool ground_ray_ready() { return g_ray.load(std::memory_order_acquire) != nullptr; }

bool ground_hook_reset_bounds(uintptr_t setter, std::string* why) {
    static std::mutex m;
    static bool hooked = false;
    std::lock_guard<std::mutex> lock(m);
    if (hooked) return true;
    if (!setter || !install_emf_hook("ResetBoundsSet (ground)", setter, reinterpret_cast<void*>(&hk_setter),
                                     reinterpret_cast<void**>(&g_o_setter))) {
        if (why) *why = "the reset-bounds setter could not be hooked";
        return false;
    }
    hooked = true;
    return true;
}

void ground_note_realm(void* program_context) {
    const uint8_t* job = nullptr;
    uint8_t realm = 0;
    if (program_context && seh_read(program_context, &job, 8) && job && seh_read(job + 0xbb, &realm, 1))
        g_realm.store(realm, std::memory_order_relaxed);
}

bool ground_limits(EmfWorld* world, GroundLimits* out, std::string* why) {
    if (!ground_ray_ready()) {
        if (why) *why = "the ground cannot be read on this game build";
        return false;
    }
    if (!g_box_known.load(std::memory_order_acquire)) {
        if (why) *why = "the world's height limit is not known yet";
        return false;
    }
    Grid g;
    if (!read_grid(world, &g, why)) return false;
    out->box_top = g_box_top.load(std::memory_order_relaxed);
    out->grid_bottom = g.oy;
    out->grid_top = g.oy + g.ny * g.size;
    char line[256];
    std::snprintf(line, sizeof line,
                  "realm %u, grid %ux%u cells of %.0f m from (%.1f, %.1f, %.1f), top %.1f, reset box top %.1f", g.realm,
                  g.nx, g.ny, g.size, g.ox, g.oy, g.oz, out->grid_top, out->box_top);
    std::lock_guard<std::mutex> lock(g_desc_m);
    g_desc = line;
    return true;
}

bool ground_ray(EmfWorld* world, double x, double z, double from_y, double to_y, RayResult* out) {
    VoxelRayFn fn = g_ray.load(std::memory_order_acquire);
    Grid g;
    if (!fn || !read_grid(world, &g, nullptr)) return false;
    if (x < g.ox || z < g.oz || x >= g.ox + g.nx * g.size || z >= g.oz + g.nx * g.size) return false;  // off the grid
    alignas(16) float from[4] = {static_cast<float>(x), static_cast<float>(from_y), static_cast<float>(z), 0};
    alignas(16) float to[4] = {static_cast<float>(x), static_cast<float>(to_y), static_cast<float>(z), 0};
    alignas(16) uint8_t hit[64] = {};
    const uint8_t options[4] = {g.realm, 0, 1, 1};  // stop at an unavailable cell (it is queued for loading)
    if (!call_ray(fn, hit, g.terrain, from, to, options)) return false;
    out->loaded = hit[0x12] != 0;
    out->hit = hit[0x13] != 0;
    float y = 0;
    std::memcpy(&y, hit + 4, sizeof y);
    out->y = y;
    return true;
}

std::string ground_describe() {
    std::lock_guard<std::mutex> lock(g_desc_m);
    return g_desc;
}

}  // namespace emf::core
