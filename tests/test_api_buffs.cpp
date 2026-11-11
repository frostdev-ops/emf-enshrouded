// API v32 buffs through the core state: the types from a world's BuffRegistry, an entity's buffs, giving and removing
// through BuffService (queued), the players' buff events after the server step (at most 4 times a second), the console
// lines.
#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/buff_service.h"
#include "core/buffs.h"
#include "emf/generated/components.hpp"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeWorld;
using emf::test::SingletonStore;
namespace ecs = emf::gen::ecs;

namespace {

template <typename T>
void put(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}

constexpr uint32_t kWet = 0xAC13C6C3u;
constexpr uint32_t kElixir = 0xFF24634Fu;

uint64_t g_now = 10000;
std::vector<std::string> g_lines;
std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> g_comps;
void* fake_gc(void* out, void*, uint32_t hash, uint32_t entity) {
    auto o = static_cast<uint64_t*>(out);
    o[0] = o[1] = 0;
    auto it = g_comps.find({hash, entity});
    if (it != g_comps.end()) {
        o[0] = reinterpret_cast<uint64_t>(it->second.data());
        o[1] = it->second.size();
    }
    return out;
}

struct Registry {
    std::vector<uint8_t> blob = std::vector<uint8_t>(0x20 + 2 * kBuffTypeSize);
    alignas(8) uint8_t reg[0x40] = {};
    Registry() {
        put<int32_t>(blob.data(), 0x18, 8);
        put<uint32_t>(blob.data(), 0x1C, 2);
        uint8_t* r = blob.data() + 0x20;
        put<uint32_t>(r, 0, kWet);
        r[4] = EMF_BUFF_DEBUFF;
        r[5] = 1;
        put<int64_t>(r, 8, 900'000'000'000LL);
        r[0x4A] = 1;
        r += kBuffTypeSize;
        put<uint32_t>(r, 0, kElixir);
        r[4] = EMF_BUFF_BUFF;
        put<int64_t>(r, 8, 1'800'000'000'000LL);
        const uint8_t* b = blob.data();
        std::memcpy(reg + 0x20, &b, 8);
    }
} g_reg;

std::vector<uint8_t>& container(uint32_t entity) {
    auto& c = g_comps[{ecs::BuffContainer::kHash, entity}];
    if (c.empty()) c.assign(kBuffContainerSize, 0);
    return c;
}
void set_buff(uint32_t entity, size_t group, int i, uint32_t buff_entity, uint32_t type) {
    auto& c = container(entity);
    put<uint32_t>(c.data(), group + i * 12, buff_entity);
    put<uint32_t>(c.data(), group + i * 12 + 4, type);
}
void set_net(uint32_t buff_entity, int64_t life, int64_t end, uint32_t type, uint32_t level) {
    std::vector<uint8_t> d(24);
    put<int64_t>(d.data(), 0, life);
    put<int64_t>(d.data(), 8, end);
    put<uint32_t>(d.data(), 16, type);
    put<uint32_t>(d.data(), 20, level);
    g_comps[{ecs::BuffNetworkData::kHash, buff_entity}] = d;
}

struct Seen {
    uint32_t kind, target, type;
    uint8_t cleansed;
};
std::vector<Seen> g_seen;
void on_buff_cb(EmfWorld*, const EmfBuffEvent* e, void*) { g_seen.push_back({e->kind, e->target, e->type, e->cleansed}); }
void faulting_cb(EmfWorld*, const EmfBuffEvent*, void*) { *static_cast<volatile int*>(nullptr) = 1; }

struct Fixture {
    CoreState state;
    FakeWorld server, client;
    SingletonStore ss;
    BuffService service;
    std::vector<std::function<void(EmfWorld*)>> after;
    Fixture() : service(deps()) {
        g_lines.clear();
        g_comps.clear();
        g_seen.clear();
        g_now = 10000;
        state.get_component = fake_gc;
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return g_now; };
        state.buff_players = [](EmfWorld*) { return std::vector<uint32_t>{1}; };
        state.buffs = &service;
        server.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        client.put<uint8_t>(off::kKind, EMF_WORLD_CLIENT);
        ss.plant(server, "BuffRegistry", g_reg.reg);
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    BuffService::Deps deps() {
        BuffService::Deps d;
        d.apply = [](uint8_t* out, uint32_t, uint32_t, const void*, void*) { return out; };
        d.after_step = [this](std::function<void(EmfWorld*)> fn) { after.push_back(std::move(fn)); };
        d.now_ms = [] { return g_now; };
        return d;
    }
    EmfWorld* w() { return reinterpret_cast<EmfWorld*>(server.p()); }
    EmfWorld* cw() { return reinterpret_cast<EmfWorld*>(client.p()); }
};

}  // namespace

TEST_CASE("api v32: version 32, size 864, the seven buff functions set") {
    const EmfApi* api = api_table();
    CHECK(EMF_API_VERSION == 32);
    CHECK(api->version == 32u);
    CHECK(api->size == kApiV32Size);
    CHECK(kApiV32Size == 864u);
    CHECK(api->buff_types);
    CHECK(api->buff_type);
    CHECK(api->buff_text);
    CHECK(api->buffs_of);
    CHECK(api->buff_apply);
    CHECK(api->buff_remove);
    CHECK(api->on_buff);
}

TEST_CASE("api v32: buff_types and buff_type from the world's BuffRegistry; nothing without one") {
    Fixture f;
    const EmfApi* api = api_table();
    CHECK(api->buff_types(f.w(), nullptr, 0) == 2);
    EmfBuffType t[4] = {};
    REQUIRE(api->buff_types(f.w(), t, 4) == 2);
    CHECK(t[0].id == kWet);
    CHECK(t[0].flags == EMF_BUFF_SAVED);
    CHECK(t[1].slot == EMF_BUFF_BUFF);
    EmfBuffType one{};
    CHECK(api->buff_type(f.w(), kElixir, &one));
    CHECK(one.default_ns == 1'800'000'000'000LL);
    CHECK_FALSE(api->buff_type(f.w(), 0x1234, &one));
    CHECK(api->buff_types(f.cw(), t, 4) == 0);  // this client world has no registry planted
    CHECK(api->buff_types(nullptr, t, 4) == 0);
    char buf[32];
    CHECK_FALSE(api->buff_text(kWet, 0, buf, sizeof buf));  // no game text here (as on the dedicated server)
}

TEST_CASE("api v32: buffs_of reads the container and the buff entities' network data") {
    Fixture f;
    set_buff(1, 0xF0, 0, 900, kWet);
    set_net(900, 900'000'000'000LL, 0, kWet, 3);  // no end yet
    set_buff(1, 0x30, 1, 901, kElixir);
    EmfBuff b[4] = {};
    REQUIRE(api_table()->buffs_of(f.w(), 1, b, 4) == 2);
    CHECK(b[0].type == kElixir);
    CHECK(b[0].slot == EMF_BUFF_BUFF);
    CHECK(b[1].type == kWet);
    CHECK(b[1].entity == 900);
    CHECK(b[1].remaining_ns == -1);
    CHECK(api_table()->buffs_of(f.w(), 77, b, 4) == 0);  // no container
}

TEST_CASE("api v32: buff_apply and buff_remove: the server world only, a known type, then queued") {
    Fixture f;
    const EmfApi* api = api_table();
    CHECK_FALSE(api->buff_apply(f.cw(), 1, kWet, 0, 0, 0, nullptr, nullptr));
    CHECK_FALSE(api->buff_apply(f.w(), 1, 0x1234, 0, 0, 0, nullptr, nullptr));
    CHECK(f.service.pending() == 0);
    CHECK(api->buff_apply(f.w(), 1, kWet, 5'000'000'000LL, 0, 0, nullptr, nullptr));
    CHECK(api->buff_remove(f.w(), 1, kWet, nullptr, nullptr));
    CHECK(f.service.pending() == 2);
    bool said = false;
    for (const auto& l : g_lines) said |= l.find("unknown buff type") != std::string::npos;
    CHECK(said);
    f.state.buffs = nullptr;
    CHECK_FALSE(api->buff_apply(f.w(), 1, kWet, 0, 0, 0, nullptr, nullptr));
}

TEST_CASE("api v32: on_buff: the first comparison says nothing, then applied, removed") {
    Fixture f;
    const EmfApi* api = api_table();
    set_buff(1, 0xF0, 0, 900, kWet);
    REQUIRE(api->on_buff(on_buff_cb, nullptr));
    CHECK_FALSE(api->on_buff(nullptr, nullptr));
    buffs_server_step(f.w());
    CHECK(g_seen.empty());
    set_buff(1, 0x30, 0, 901, kElixir);
    g_now += 16;
    buffs_server_step(f.w());
    REQUIRE(g_seen.size() == 1);
    CHECK(g_seen[0].kind == EMF_BUFF_APPLIED);
    CHECK(g_seen[0].target == 1);
    CHECK(g_seen[0].type == kElixir);
    set_buff(1, 0xF0, 0, 0, 0);
    g_now += 300;
    buffs_server_step(f.w());
    REQUIRE(g_seen.size() == 2);
    CHECK(g_seen[1].kind == EMF_BUFF_REMOVED);
    CHECK(g_seen[1].type == kWet);
    buffs_new_session();  // a new world: a first sight again
    set_buff(1, 0x30, 2, 902, kWet);
    g_now += 300;
    buffs_server_step(f.w());
    CHECK(g_seen.size() == 2);
}

TEST_CASE("api v32: a faulting on_buff callback is switched off; 16 at most") {
    Fixture f;
    const EmfApi* api = api_table();
    REQUIRE(api->on_buff(faulting_cb, nullptr));
    REQUIRE(api->on_buff(on_buff_cb, nullptr));
    container(1);  // the player has a container from the first comparison on
    buffs_server_step(f.w());
    set_buff(1, 0x30, 0, 901, kElixir);
    g_now += 300;
    buffs_server_step(f.w());
    set_buff(1, 0x30, 1, 903, kWet);
    g_now += 300;
    buffs_server_step(f.w());
    CHECK(g_seen.size() == 2);  // the good one got both; the faulting one was switched off after its first
    for (int i = 2; i < 16; ++i) CHECK(api->on_buff(on_buff_cb, nullptr));
    CHECK_FALSE(api->on_buff(on_buff_cb, nullptr));
}

TEST_CASE("buff lines: a type and an active buff as the console shows them") {
    EmfBuffType t{};
    t.id = kWet;
    t.slot = EMF_BUFF_DEBUFF;
    t.default_ns = 900'000'000'000LL;
    t.flags = EMF_BUFF_SAVED | EMF_BUFF_HIDDEN;
    CHECK(buff_type_line(t, "Wet") == "0xAC13C6C3 debuff 900 s Wet (saved, hidden)");
    CHECK(buff_type_line(t, "") == "0xAC13C6C3 debuff 900 s - (saved, hidden)");
    EmfBuff b{};
    b.type = kWet;
    b.slot = EMF_BUFF_DEBUFF;
    b.entity = 900;
    b.remaining_ns = 61'500'000'000LL;
    b.total_ns = 900'000'000'000LL;
    CHECK(buff_line(b, "Wet") == "0xAC13C6C3 debuff Wet: 61 s of 900 s left (entity 900)");
    b.remaining_ns = b.total_ns = -1;
    b.cleansed = 1;
    CHECK(buff_line(b, "") == "0xAC13C6C3 debuff -: time not known yet (entity 900), being removed");
}

TEST_CASE("api v32: on_buff compares every step: a cleanse seen for one step is removed with cleansed 1; the player list is read 4 times a second") {
    Fixture f;
    static int reads = 0;
    reads = 0;
    f.state.buff_players = [](EmfWorld*) {
        ++reads;
        return std::vector<uint32_t>{1};
    };
    set_buff(1, 0x30, 0, 901, kElixir);
    REQUIRE(api_table()->on_buff(on_buff_cb, nullptr));
    buffs_server_step(f.w());  // first sight
    container(1)[0x30 + 8] = 1;  // cleansed by a mod in this step
    g_now += 16;
    buffs_server_step(f.w());
    set_buff(1, 0x30, 0, 0, 0);  // the game clears the slot the next step
    g_now += 16;
    buffs_server_step(f.w());
    REQUIRE(g_seen.size() == 1);
    CHECK(g_seen[0].kind == EMF_BUFF_REMOVED);
    CHECK(g_seen[0].cleansed == 1);
    CHECK(reads == 1);  // the player list once in these 32 ms
    g_now += 300;
    buffs_server_step(f.w());
    CHECK(reads == 2);
}

TEST_CASE("api v32: buff_apply refuses an item id the world does not know (review I2)") {
    Fixture f;
    const EmfApi* api = api_table();
    CHECK_FALSE(api->buff_apply(f.w(), 1, kWet, 0, 0, 0x1234, nullptr, nullptr));
    CHECK(f.service.pending() == 0);
    bool said = false;
    for (const auto& l : g_lines) said |= l.find("unknown item") != std::string::npos;
    CHECK(said);
    CHECK(api->buff_apply(f.w(), 1, kWet, 0, 0, 0, nullptr, nullptr));  // no item: fine
}

TEST_CASE("api v32: the same refusal within 10 s is logged once (review M3: a mod calling every tick)") {
    Fixture f;
    const EmfApi* api = api_table();
    for (int i = 0; i < 5; ++i) CHECK_FALSE(api->buff_apply(f.cw(), 1, kWet, 0, 0, 0, nullptr, nullptr));
    int lines = 0;
    for (const auto& l : g_lines) lines += l.find("server world only") != std::string::npos ? 1 : 0;
    CHECK(lines == 1);
}
