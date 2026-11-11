// Buffs and status effects (API v32, private research buffs_2026-11-11): the game-free part: the registry's buff type
// collection, an entity's container, the 0xE8-byte ApplyBuffCtx, the check after applyBuffCore, the cleanse, and the
// comparison that makes the players' buff events.
#include <doctest.h>

#include <cstring>
#include <map>
#include <vector>

#include "common/fnv.h"
#include "core/api_sizes.h"
#include "core/buffs.h"

using namespace emf::core;

namespace {

template <typename T>
void put(uint8_t* p, size_t off, T v) {
    std::memcpy(p + off, &v, sizeof v);
}
template <typename T>
T get(const uint8_t* p, size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

// A BuffTypeCollectionResource blob as the research describes it: the buffs BlobArray {s32 rel, u32 count} at +0x18
// (rel from the field's own address), records of 0xB8 bytes.
struct FakeRegistry {
    std::vector<uint8_t> blob;
    uint8_t registry[0x40] = {};
    explicit FakeRegistry(uint32_t n) : blob(0x20 + n * kBuffTypeSize) {
        put<int32_t>(blob.data(), 0x18, 0x20 - 0x18);
        put<uint32_t>(blob.data(), 0x1C, n);
        const uint8_t* b = blob.data();
        std::memcpy(registry + 0x20, &b, 8);
    }
    uint8_t* rec(uint32_t i) { return blob.data() + 0x20 + i * kBuffTypeSize; }
};

void guid(uint8_t* p, uint8_t seed) {
    for (int i = 0; i < 16; ++i) p[i] = static_cast<uint8_t>(seed + i);
}

constexpr uint32_t kWet = 0xAC13C6C3u;
constexpr uint32_t kPoisoned = 0x369C3618u;

void fill_wet(uint8_t* r) {
    put<uint32_t>(r, 0x00, kWet);
    r[0x04] = EMF_BUFF_DEBUFF;
    r[0x05] = 1;  // ResetTime
    put<int64_t>(r, 0x08, 900'000'000'000LL);
    guid(r + 0x10, 1);
    guid(r + 0x20, 50);
    r[0x4A] = 1;  // isSaved
}

void fill_poisoned(uint8_t* r) {
    put<uint32_t>(r, 0x00, kPoisoned);
    r[0x04] = EMF_BUFF_DEBUFF;
    r[0x05] = 1;
    put<int64_t>(r, 0x08, 10'000'000'000LL);
    r[0x31] = 1;  // hideInUi
    r[0x49] = 1;  // despawnOnDeath
    put<uint16_t>(r, 0x5C, 0xFF7F);  // every faction but players
    guid(r + 0x88, 9);  // a follow-up
    r[0x98] = 1;
}

// A BuffContainer (472 bytes) with instances {entity, type, cleansed}.
struct Container {
    uint8_t b[kBuffContainerSize] = {};
    void set(size_t group_off, int i, uint32_t entity, uint32_t type, uint8_t cleansed = 0) {
        put<uint32_t>(b, group_off + i * 12, entity);
        put<uint32_t>(b, group_off + i * 12 + 4, type);
        b[group_off + i * 12 + 8] = cleansed;
    }
};
constexpr size_t kFood = 0x000, kBuffs = 0x030, kDebuffs = 0x0F0, kRested = 0x1B0, kTerrain = 0x1BC;

std::map<uint32_t, BuffNet> g_net;
BuffNet net_of(uint32_t e, void*) {
    const auto it = g_net.find(e);
    return it == g_net.end() ? BuffNet{} : it->second;
}

}  // namespace

TEST_CASE("buff api: v32 layouts") {
    CHECK(EMF_API_VERSION == 32);
    CHECK(sizeof(EmfBuffType) == 40);
    CHECK(offsetof(EmfBuffType, default_ns) == 16);
    CHECK(offsetof(EmfBuffType, name_tag) == 28);
    CHECK(sizeof(EmfBuff) == 40);
    CHECK(offsetof(EmfBuff, level) == 16);
    CHECK(offsetof(EmfBuff, total_ns) == 24);
    CHECK(sizeof(EmfBuffEvent) == 48);
    CHECK(offsetof(EmfBuffEvent, total_ns) == 24);
    CHECK(offsetof(EmfBuffEvent, why) == 40);
    CHECK(kApiV32Size == kApiV31Size + 7 * 8);
}

TEST_CASE("buff types: the registry's collection, a record by id, its fields") {
    FakeRegistry reg(2);
    fill_wet(reg.rec(0));
    fill_poisoned(reg.rec(1));
    BuffCollection c;
    REQUIRE(buff_collection(reg.registry, &c));
    CHECK(c.count == 2);
    CHECK(buff_record(c, kPoisoned) == reg.rec(1));
    CHECK(buff_record(c, 0x12345678u) == nullptr);
    EmfBuffType t{};
    REQUIRE(buff_type_of(reg.rec(0), &t));
    CHECK(t.size == sizeof t);
    CHECK(t.id == kWet);
    CHECK(t.slot == EMF_BUFF_DEBUFF);
    CHECK(t.apply_type == 1);
    CHECK(t.default_ns == 900'000'000'000LL);
    CHECK(t.flags == EMF_BUFF_SAVED);
    CHECK(t.follow_up == 0);
    CHECK(t.name_tag == emf::fnv1a32(reg.rec(0) + 0x10, 16));
    CHECK(t.desc_tag == emf::fnv1a32(reg.rec(0) + 0x20, 16));
    REQUIRE(buff_type_of(reg.rec(1), &t));
    CHECK(t.flags == (EMF_BUFF_HIDDEN | EMF_BUFF_DESPAWN_ON_DEATH));
    CHECK(t.faction_exclude == 0xFF7F);
    CHECK(t.follow_up == emf::fnv1a32(reg.rec(1) + 0x88, 16));
    CHECK(t.name_tag == 0);  // no name: an all-zero tag
}

TEST_CASE("buff types: a registry without a collection, a bad count or a null pointer is refused") {
    BuffCollection c;
    CHECK_FALSE(buff_collection(nullptr, &c));
    uint8_t empty[0x40] = {};
    CHECK_FALSE(buff_collection(empty, &c));
    FakeRegistry reg(1);
    put<uint32_t>(reg.blob.data(), 0x1C, 100000);  // nonsense count
    CHECK_FALSE(buff_collection(reg.registry, &c));
    put<uint32_t>(reg.blob.data(), 0x1C, 0);
    CHECK_FALSE(buff_collection(reg.registry, &c));
    const uintptr_t bad = 0x10;  // a pointer into nothing
    std::memcpy(reg.registry + 0x20, &bad, 8);
    CHECK_FALSE(buff_collection(reg.registry, &c));
}

TEST_CASE("buff container: the active buffs in slot order with time left; food has 3 slots unless extended") {
    Container k;
    k.set(kFood, 3, 900, 0x11);  // the 4th food slot: not used without the perk
    k.set(kBuffs, 2, 901, 0x22);
    k.set(kDebuffs, 0, 902, kWet);
    k.set(kTerrain, 0, 903, 0x44, 1);
    k.set(kRested, 0, 904, 0x55);
    g_net = {{902, BuffNet{900'000'000'000LL, 1'000'000'000'000LL, kWet, 2, true}}};
    EmfBuff out[8] = {};
    const uint32_t n = read_buffs(k.b, &net_of, nullptr, 400'000'000'000LL, out, 8);
    REQUIRE(n == 4);
    CHECK(out[0].type == 0x22);
    CHECK(out[0].slot == EMF_BUFF_BUFF);
    CHECK(out[0].index == 2);
    CHECK(out[0].total_ns == -1);  // no network data yet
    CHECK(out[0].remaining_ns == -1);
    CHECK(out[1].type == kWet);
    CHECK(out[1].entity == 902);
    CHECK(out[1].slot == EMF_BUFF_DEBUFF);
    CHECK(out[1].level == 2);
    CHECK(out[1].total_ns == 900'000'000'000LL);
    CHECK(out[1].remaining_ns == 600'000'000'000LL);
    CHECK(out[2].slot == EMF_BUFF_RESTED);
    CHECK(out[3].slot == EMF_BUFF_TERRAIN);
    CHECK(out[3].cleansed == 1);
    k.b[0x1D0] = 1;  // extendedFoodBuffSlots
    CHECK(read_buffs(k.b, &net_of, nullptr, 0, out, 8) == 5);
    CHECK(out[0].slot == EMF_BUFF_FOOD);
    CHECK(out[0].index == 3);
    CHECK(read_buffs(k.b, &net_of, nullptr, 0, out, 2) == 5);  // the count, more than cap
    CHECK(read_buffs(k.b, &net_of, nullptr, 0, nullptr, 0) == 5);
}

TEST_CASE("buff apply ctx: the 0xE8 bytes at the research's offsets") {
    BuffHandles h;
    h.program_context = reinterpret_cast<void*>(0x1000);
    h.container_w = 0x21;
    h.lifetime_w = 0x22;
    h.faction_r = 0x23;
    h.skill_tree = reinterpret_cast<void*>(0x2000);
    h.balancing = reinterpret_cast<void*>(0x3000);
    h.item_registry = reinterpret_cast<void*>(0x4000);
    for (int i = 0; i < 11; ++i) h.source[i] = 0x100 + i;
    h.events = reinterpret_cast<void*>(0x5000);
    BuffApplyArgs a;
    a.duration_ns = 30'000'000'000LL;
    a.item = 0xBEEF;
    a.with_source = true;
    a.target_skills_ui = reinterpret_cast<void*>(0x6000);
    a.target_wet_reduction = reinterpret_cast<void*>(0x7000);
    uint8_t c[kApplyBuffCtxSize];
    std::memset(c, 0xCC, sizeof c);
    fill_apply_ctx(c, h, a);
    CHECK(get<uint64_t>(c, 0x00) == 0x1000);
    CHECK(get<uint64_t>(c, 0x08) == 0);
    CHECK(get<uint32_t>(c, 0x10) == 0xBEEF);
    CHECK(get<uint32_t>(c, 0x1C) == 0);
    CHECK(c[0x20] == 0);
    CHECK(get<uint64_t>(c, 0x28) == 0x21);
    CHECK(get<uint64_t>(c, 0x30) == 0x22);
    CHECK(get<uint64_t>(c, 0x38) == 0x6000);
    CHECK(get<uint64_t>(c, 0x40) == 0x2000);
    CHECK(get<uint64_t>(c, 0x48) == 0x7000);
    CHECK(get<uint64_t>(c, 0x50) == 0);  // no actor: no dead check (the native passes NULL too)
    CHECK(get<uint64_t>(c, 0x58) == 0);
    CHECK(get<int64_t>(c, 0x60) == 30'000'000'000LL);
    CHECK(get<uint64_t>(c, 0x68) == 0x100);
    CHECK(get<uint64_t>(c, 0xB8) == 0x10A);
    CHECK(get<uint64_t>(c, 0xC0) == 0x23);
    CHECK(get<uint64_t>(c, 0xC8) == 0x4000);
    CHECK(get<uint64_t>(c, 0xD0) == 0x3000);
    CHECK(get<uint64_t>(c, 0xD8) == 0x5000);
    CHECK(get<uint64_t>(c, 0xE0) == 0x5000);
    a.with_source = false;
    a.item = 0;
    fill_apply_ctx(c, h, a);
    CHECK(get<uint64_t>(c, 0x68) == 0);  // no source: its stats are not copied
    CHECK(get<uint64_t>(c, 0xB8) == 0);
    CHECK(get<uint64_t>(c, 0xC8) == 0);  // no item: no item registry
}

TEST_CASE("buff check: applied only when the slot group holds the type; the reasons of a refusal") {
    Container k;
    k.set(kDebuffs, 4, 950, kWet);
    BuffCheck r = check_applied(0, k.b, kWet, EMF_BUFF_DEBUFF, 0, -1);
    CHECK(r.applied);
    r = check_applied(0, k.b, kPoisoned, EMF_BUFF_DEBUFF, 0xFF7F, 6);  // a wild beast
    CHECK_FALSE(r.applied);
    CHECK(r.why.find("faction") != std::string::npos);
    r = check_applied(0, k.b, kPoisoned, EMF_BUFF_DEBUFF, 0, -1);
    CHECK_FALSE(r.applied);
    CHECK(r.why.find("faction could not be read") != std::string::npos);  // EMF never passes the actor: no dead check
    r = check_applied(0x14, k.b, kPoisoned, EMF_BUFF_DEBUFF, 0, -1);
    CHECK(r.why.find("no free debuff slot") != std::string::npos);
    r = check_applied(0x11, k.b, kWet, EMF_BUFF_DEBUFF, 0, -1);
    CHECK(r.why.find("again") != std::string::npos);
    r = check_applied(0x12, k.b, kWet, EMF_BUFF_DEBUFF, 0, -1);
    CHECK(r.why.find("no buff container") != std::string::npos);
    r = check_applied(0, k.b, kWet, EMF_BUFF_BUFF, 0, -1);  // the type in another group does not count
    CHECK_FALSE(r.applied);
}

TEST_CASE("buff cleanse: the first instance of the type gets cleansed = 1; none: false, nothing written") {
    Container k;
    k.set(kBuffs, 1, 960, 0x77);
    k.set(kDebuffs, 3, 961, kWet);
    k.set(kFood, 3, 962, 0x88);  // the 4th food slot counts here (the game's cleanse looks at all four)
    Container before = k;
    CHECK_FALSE(buff_cleanse(k.b, 0x99));
    CHECK(std::memcmp(k.b, before.b, sizeof k.b) == 0);
    CHECK(buff_cleanse(k.b, kWet));
    CHECK(k.b[kDebuffs + 3 * 12 + 8] == 1);
    CHECK(buff_cleanse(k.b, 0x88));
    CHECK(k.b[kFood + 3 * 12 + 8] == 1);
    CHECK(buff_has(k.b, 0x77));
    CHECK_FALSE(buff_has(k.b, 0x99));
}

TEST_CASE("buff diff: the first sight records only; then applied, refreshed and removed (cleansed or ended)") {
    BuffDiff d;
    Container k;
    k.set(kDebuffs, 0, 970, kWet);
    g_net = {{970, BuffNet{900, 1000, kWet, 0, true}}};
    CHECK(d.step(1, k.b, &net_of, nullptr, 100).empty());  // first sight
    k.set(kBuffs, 0, 971, 0x22);  // a new buff, no network data yet
    auto ev = d.step(1, k.b, &net_of, nullptr, 200);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == EMF_BUFF_APPLIED);
    CHECK(ev[0].target == 1);
    CHECK(ev[0].type == 0x22);
    CHECK(ev[0].buff_entity == 971);
    CHECK(ev[0].slot == EMF_BUFF_BUFF);
    CHECK(ev[0].remaining_ns == -1);
    g_net[971] = BuffNet{500, 700, 0x22, 0, true};  // its data arrives: not a refresh
    CHECK(d.step(1, k.b, &net_of, nullptr, 210).empty());
    g_net[970] = BuffNet{900, 1000 + 3'000'000'000LL, kWet, 0, true};  // Wet given again: 3 s later
    ev = d.step(1, k.b, &net_of, nullptr, 400);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == EMF_BUFF_REFRESHED);
    CHECK(ev[0].remaining_ns == 3'000'000'600LL);
    k.set(kDebuffs, 0, 970, kWet, 1);  // cleansed: still there this step
    CHECK(d.step(1, k.b, &net_of, nullptr, 410).empty());
    k.set(kDebuffs, 0, 0, 0);  // gone
    k.set(kBuffs, 0, 0, 0);
    ev = d.step(1, k.b, &net_of, nullptr, 420);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == EMF_BUFF_REMOVED);
    CHECK(ev[0].type == 0x22);
    CHECK(ev[0].cleansed == 0);  // ended
    CHECK(ev[1].type == kWet);
    CHECK(ev[1].cleansed == 1);
    d.forget(1);
    k.set(kBuffs, 0, 980, 0x33);
    CHECK(d.step(1, k.b, &net_of, nullptr, 500).empty());  // forgotten: a first sight again
    d.reset();
    CHECK(d.step(1, k.b, &net_of, nullptr, 600).empty());
    d.retain({});
    CHECK(d.size() == 0);
}

TEST_CASE("buff diff: rain re-applying Wet every step (its end creeping on) is no refresh; a jump of over a second is") {
    BuffDiff d;
    Container k;
    k.set(kDebuffs, 0, 970, kWet);
    const int64_t s = 1'000'000'000LL;
    int64_t now = 100 * s;
    g_net = {{970, BuffNet{900 * s, now + 900 * s, kWet, 0, true}}};
    CHECK(d.step(1, k.b, &net_of, nullptr, now).empty());
    for (int i = 0; i < 8; ++i) {  // 250 ms later each time, the end 250 ms later too
        now += s / 4;
        g_net[970].end = now + 900 * s;
        CHECK(d.step(1, k.b, &net_of, nullptr, now).empty());
    }
    now += s / 4;
    g_net[970].end = now + 900 * s + 5 * s;  // given again with more time
    const auto ev = d.step(1, k.b, &net_of, nullptr, now);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == EMF_BUFF_REFRESHED);
}

TEST_CASE("buff diff: a replace says removed (replaced) before applied (review I1)") {
    BuffDiff d;
    Container k;
    k.set(kBuffs, 0, 970, 0x22);
    CHECK(d.step(1, k.b, &net_of, nullptr, 100).empty());
    k.set(kBuffs, 0, 971, 0x22);  // given again: a Replace type gets a new buff entity in the slot
    const auto ev = d.step(1, k.b, &net_of, nullptr, 200);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].kind == EMF_BUFF_REMOVED);
    CHECK(ev[0].buff_entity == 970);
    CHECK(ev[0].replaced == 1);
    CHECK(ev[1].kind == EMF_BUFF_APPLIED);
    CHECK(ev[1].buff_entity == 971);
    CHECK(ev[1].replaced == 0);
    k.set(kBuffs, 0, 0, 0);
    const auto gone = d.step(1, k.b, &net_of, nullptr, 300);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].replaced == 0);  // it ended: nothing took its place
}

TEST_CASE("buff cleanse: an instance already being removed does not count again (polish: two removes)") {
    Container k;
    k.set(kDebuffs, 3, 961, kWet, 1);  // cleansed: gone after the next step
    CHECK_FALSE(buff_has(k.b, kWet));
    CHECK_FALSE(buff_cleanse(k.b, kWet));
    k.set(kDebuffs, 4, 962, kWet);  // a second Wet that is not being removed
    CHECK(buff_has(k.b, kWet));
    CHECK(buff_cleanse(k.b, kWet));
    CHECK(k.b[kDebuffs + 4 * 12 + 8] == 1);
}
