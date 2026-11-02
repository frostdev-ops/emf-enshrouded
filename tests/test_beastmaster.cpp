// Beastmaster (mods/beastmaster), run by the Lua host from a copy of the shipped folder, with fake engine events,
// components and commands (tests/lua_mod_rig.h).
#include "lua_mod_rig.h"

namespace {
// The animation graph the game wolf (and the scavengers' hound, and the bear pack's bear) runs: its template data on the
// server world, where render models are not (seen in game: a spawned wolf has no ModelResource there).
constexpr const char* kWolfGraph = "3180813f-a50d-47fc-be92-29a2383bd47d";

// The game stores a GUID's first three groups little-endian (see read_field in src/lua_host/fields.cpp).
std::vector<uint8_t> guid_bytes(const std::string& text) {
    auto hex = [&](size_t at) { return static_cast<uint8_t>(std::stoul(text.substr(at, 2), nullptr, 16)); };
    return {hex(6), hex(4), hex(2), hex(0), hex(11), hex(9), hex(16), hex(14),
            hex(19), hex(21), hex(24), hex(26), hex(28), hex(30), hex(32), hex(34)};
}

// Player 1 (account 0xAA, the host, level 10) at (0, 10, 0); player 2 (0xBB) at (30, 10, 0).
struct BeastRig : ModRig {
    explicit BeastRig(const std::map<std::string, std::string>& settings = {})
        : ModRig("beastmaster", L"beastmaster", settings, [](Fake& f) {
              add_player(f, 1, 0xAA, 0, 10, 0);
              add_player(f, 2, 0xBB, 30, 10, 0);
              for (uint32_t p : {1u, 2u}) {
                  component<ecs::Level>(f, p).dataStorage[0] = 10;
                  add_entity(f, p);
                  f.attrs[{p, EMF_ATTR_MANA}] = 100;
                  f.attrs[{p, EMF_ATTR_MAX_MANA}] = 100;
              }
          }) {
        with_attributes(r.api);  // a v18+ core: health, mana and damage through the game's attributes
        with_teleport(r.api);    // a v20 core
    }
    std::string saved() { return slurp(data); }
    // Player `p`'s camera at (x, y, z) looking along `yaw_deg` (0 = +z, 90 = +x), level.
    void look(uint32_t p, double x, double y, double z, double yaw_deg) {
        auto& in = component<ecs::PlayerInput>(r.fake, p);
        auto& cam = in.fromClient.camera;
        cam.position.x = static_cast<int64_t>(x * 4294967296.0);
        cam.position.y = static_cast<int64_t>(y * 4294967296.0);
        cam.position.z = static_cast<int64_t>(z * 4294967296.0);
        const double h = yaw_deg * 3.14159265358979 / 360.0;
        cam.orientation.x = 0;
        cam.orientation.y = static_cast<float>(std::sin(h));
        cam.orientation.z = 0;
        cam.orientation.w = static_cast<float>(std::cos(h));
    }
    // A creature running the animation graph `graph` (template data) at (x, 10, z), of `faction`, with health.
    void beast(uint32_t id, const char* graph, double x, double z, int faction, int32_t cur, int32_t max) {
        place(r.fake, id, x, 10, z);
        auto g = guid_bytes(graph);
        g.resize(ecs::AnimationGraphResourceComponent2::kSize, 0);
        r.fake.templates[{ecs::AnimationGraphResourceComponent2::kHash, id}] = g;
        component<ecs::Faction>(r.fake, id).faction = static_cast<emf::gen::FactionType>(faction);
        r.fake.attrs[{id, EMF_ATTR_HEALTH}] = cur;
        r.fake.attrs[{id, EMF_ATTR_MAX_HEALTH}] = max;
        add_entity(r.fake, id);
    }
    int faction(uint32_t id) { return r.fake.components[{ecs::Faction::kHash, id}][0]; }
    // StartGuardingEvents sent so far: (guarding, guarded)
    std::vector<std::pair<uint32_t, uint32_t>> guards() {
        send_events();
        std::vector<std::pair<uint32_t, uint32_t>> out;
        for (const auto& [name, b] : r.fake.emitted) {
            if (name != "StartGuardingEvent") continue;
            const auto* e = reinterpret_cast<const ecs::StartGuardingEvent*>(b.data());
            out.push_back({e->guardingEntityId.id, e->guardedEntityId.id});
        }
        return out;
    }
    // The creature the game makes for the next `n` spawns (ids 1234, 1235, ...): wild, level 1, 100 health.
    void spawnable(int n) {
        for (int i = 0; i < n; ++i) {
            const uint32_t id = r.fake.next_spawn_id + static_cast<uint32_t>(i);
            component<ecs::Faction>(r.fake, id).faction = static_cast<emf::gen::FactionType>(6);
            component<ecs::Level>(r.fake, id).dataStorage[0] = 1;
            r.fake.attrs[{id, EMF_ATTR_HEALTH}] = 100;
            r.fake.attrs[{id, EMF_ATTR_MAX_HEALTH}] = 100;
            add_entity(r.fake, id);
        }
    }
    bool added(uint32_t entity, uint32_t attr, int32_t delta) {
        for (const auto& [e, a, d] : r.fake.attr_adds)
            if (e == entity && a == attr && d == delta) return true;
        return false;
    }
};
constexpr int kWildBeast = 6, kPlayerAlly = 11;
constexpr uint32_t kDogBait = 0x4d0a0001;
}  // namespace

TEST_CASE("beastmaster: the roster names beasts uniquely, holds 8 per player, and comes back from the world's data") {
    BeastRig b;
    for (int i = 1; i <= 8; ++i) CHECK(b.reply("beastmaster.give wolf").find("added") != std::string::npos);
    const std::string ninth = b.reply("beastmaster.give wolf");
    CHECK(ninth.find("8") != std::string::npos);
    CHECK(ninth.find("added") == std::string::npos);
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("1. Wolf - wolf, at home") != std::string::npos);
    CHECK(list.find("2. Wolf 2 - wolf") != std::string::npos);
    CHECK(list.find("8. Wolf 8 - wolf") != std::string::npos);
    b.seconds(2);
    CHECK(b.saved().find("b 0x00000000000000aa wolf ") != std::string::npos);
    // another player's collection is their own
    CHECK(b.reply("beastmaster.list", 2, 0xBB).find("no beasts") != std::string::npos);
    // the same world loads again later, with a different collection saved
    write(b.data, "b 0x00000000000000aa boar 0 0 Tusker\nb 0x00000000000000bb wolf 0 0 Grey\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    const std::string again = b.reply("beastmaster.list");
    CHECK(again.find("Tusker") != std::string::npos);
    CHECK(again.find("Wolf 2") == std::string::npos);
    CHECK(b.reply("beastmaster.list", 2, 0xBB).find("Grey") != std::string::npos);
}

TEST_CASE("beastmaster: a beast just tamed cannot fall for 5 s, then can again") {
    // a swing it began as a wild beast can land on itself once it is an ally (seen in game: a tamed minotaur's
    // three-punch combo, 2 of 3 tames, killed it just after the channel)
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    auto& h = component<ecs::Health>(b.r.fake, 50);  // the game's Health storage: [0] health, [1] its minimum
    REQUIRE(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    b.seconds(1);
    CHECK(h.dataStorage[1] == 0);   // the channel: as it was
    b.seconds(3);                   // tamed, up to 50 % (50 of 100)
    REQUIRE(b.reply("beastmaster.list").find("1. Wolf - wolf, out") != std::string::npos);
    CHECK(h.dataStorage[1] == 50);  // its health cannot go under that
    h.dataStorage[1] = 0;           // (the game may write it back)
    b.seconds(2);
    CHECK(h.dataStorage[1] == 50);  // still held
    b.seconds(4);
    CHECK(h.dataStorage[1] == 0);   // 5 s after: as before
}

TEST_CASE("beastmaster: species are known by animation graph and faction") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6") == "wolf");   // WildBeast
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 4") == "hound");  // Scavenger
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 3") == "unknown");
    CHECK(b.reply("beastmaster.species aed0ecec-8a98-4fd3-a450-ab4c508b0ad4 3") == "boar");  // also the Vukah's boars
    CHECK(b.reply("beastmaster.species 9c1b120e-4901-495c-9cc0-5de134fbe140 6") == "sabretooth");
    CHECK(b.reply("beastmaster.species 00000000-0000-0000-0000-000000000001 6") == "unknown");
    CHECK(b.reply("beastmaster.give dragon").find("no species dragon") != std::string::npos);
}

TEST_CASE("beastmaster: a wolf at 20 % health in front of you is tamed after a 3 s channel") {
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    // it calms down during the channel: an ally that comes to you (it stops fleeing, and your beasts leave it alone)
    CHECK(b.faction(50) == kPlayerAlly);
    const auto calm = b.guards();
    REQUIRE(calm.size() == 1);
    CHECK(calm[0] == std::make_pair(50u, 1u));
    CHECK(b.reply("beastmaster.list").find("no beasts") != std::string::npos);  // not yours before the channel ends
    b.seconds(4);
    CHECK(b.faction(50) == kPlayerAlly);
    for (const auto& g : b.guards()) CHECK(g == std::make_pair(50u, 1u));
    CHECK(b.added(50, EMF_ATTR_HEALTH, 30));  // up to 50 %
    CHECK(b.noted("You tamed a wolf: Wolf"));
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, out") != std::string::npos);
}

TEST_CASE("beastmaster: the channel breaks when you walk away (more than 8 m) or the beast dies") {
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    b.reply("beastmaster.tame");
    CHECK(b.faction(50) == kPlayerAlly);  // calm during the channel
    place(b.r.fake, 1, 0, 10, -10);  // 15 m away now
    b.seconds(4);
    CHECK(b.faction(50) == kWildBeast);  // wild again
    CHECK(b.noted("too far"));
    place(b.r.fake, 1, 0, 10, 0);
    b.reply("beastmaster.tame");
    deliver(b.r.fake, "EntityDiedEvent", died_event(50));
    b.r.fake.attrs[{50, EMF_ATTR_HEALTH}] = 0;
    b.seconds(4);
    CHECK(b.faction(50) == kWildBeast);
    CHECK(b.noted("died"));
    CHECK(b.reply("beastmaster.list").find("no beasts") != std::string::npos);
}

TEST_CASE("beastmaster: a healthy beast is not tamed by defeat; with 3 baits it is tamed by feeding") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 100, 100);
    CHECK(b.reply("beastmaster.tame").find("too strong") != std::string::npos);
    for (int i = 0; i < 3; ++i) {
        b.reply("beastmaster.feed");
        REQUIRE(b.r.fake.takes.size() == static_cast<size_t>(i + 1));
        const EmfItemsRequest t = b.r.fake.takes.back();
        CHECK(t.item == kDogBait);
        CHECK(t.count == 1u);
        CHECK(t.entity == 1u);
        t.cb(kServer, t.entity, t.item, t.count, 1, t.user);  // one bait was taken
        b.seconds(1);
    }
    CHECK(b.faction(50) == kPlayerAlly);
    CHECK(b.noted("You tamed a wolf"));
}

TEST_CASE("beastmaster: a beast that is already someone's (Faction 11) or of an unknown species is refused") {
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kPlayerAlly, 10, 100);
    CHECK(b.reply("beastmaster.tame").find("no beast to tame") != std::string::npos);
    b.beast(50, "00000000-0000-0000-0000-000000000099", 0, 5, kWildBeast, 10, 100);
    CHECK(b.reply("beastmaster.tame").find("no beast to tame") != std::string::npos);
    b.seconds(4);
    CHECK(b.faction(50) == kWildBeast);
}

namespace {
std::string guid_text(const uint8_t g[16]) {
    char b[40];
    std::snprintf(b, sizeof b, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2], g[1],
                  g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}
constexpr const char* kWolfTemplate = "478381c5-585f-4264-b373-dbb1ea596eca";
bool contains(const std::vector<uint32_t>& v, uint32_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }
}  // namespace

TEST_CASE("beastmaster: calling a wolf costs 15 % mana, spawns it beside you as your ally, and a second call of a wolf is refused") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");
    b.spawnable(2);
    CHECK(b.reply("beastmaster.call 1").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kWolfTemplate);
    CHECK(std::abs(b.r.fake.spawns[0].x - 0.0) + std::abs(b.r.fake.spawns[0].z - 0.0) <= 3.0);  // beside you
    CHECK(b.added(1, EMF_ATTR_MANA, -15));
    CHECK(b.faction(1234) == kPlayerAlly);
    CHECK(b.r.fake.components[{ecs::Level::kHash, 1234}][8] == 10);  // the owner's level (dataStorage[0])
    const auto g = b.guards();
    CHECK(std::find(g.begin(), g.end(), std::make_pair(1234u, 1u)) != g.end());
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, out") != std::string::npos);
    CHECK(b.reply("beastmaster.call Wolf 2").find("out already") != std::string::npos);
    CHECK(b.r.fake.spawns.size() == 1);
}

TEST_CASE("beastmaster: at most 2 out; a 10 s cooldown; too little mana gives a note and nothing else") {
    BeastRig b;
    for (const char* s : {"wolf", "boar", "hound"}) b.reply(std::string("beastmaster.give ") + s);
    b.spawnable(3);
    b.reply("beastmaster.call Wolf");
    CHECK(b.reply("beastmaster.call Boar").find("wait") != std::string::npos);  // the cooldown
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Hound").find("2 beasts out") != std::string::npos);
    CHECK(b.r.fake.spawns.size() == 2);
    b.reply("beastmaster.dismiss Boar");
    CHECK(contains(b.r.fake.destroyed, 1235));
    b.r.fake.attrs[{1, EMF_ATTR_MANA}] = 5;
    CHECK(b.reply("beastmaster.call Hound").find("Not enough mana") != std::string::npos);
    CHECK(b.r.fake.spawns.size() == 2);
}

TEST_CASE("beastmaster: a beast that dies rests 120 s, its body is removed, and it can be called again after") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(2);
    b.reply("beastmaster.call Wolf");
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 0;
    b.seconds(2);
    CHECK(contains(b.r.fake.destroyed, 1234));  // no loot from our own beasts
    CHECK(b.reply("beastmaster.list").find("rests 1:5") != std::string::npos);
    CHECK(b.reply("beastmaster.call Wolf").find("rests") != std::string::npos);
    b.seconds(120);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    CHECK(b.r.fake.spawns.size() == 2);
}

TEST_CASE("beastmaster: after a reload the beasts that were out come back once (no mana), in the same world only") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa wolf 0 1 Fang\n");
    b.spawnable(2);
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(2);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kWolfTemplate);
    CHECK_FALSE(b.added(1, EMF_ATTR_MANA, -15));
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 1);  // once
    CHECK(b.faction(1234) == kPlayerAlly);
}

TEST_CASE("beastmaster: a remembered id that now names another entity is dropped, never written") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(1);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    REQUIRE(b.faction(1234) == kPlayerAlly);
    // the wolf is gone without a death event and its id now names a scavenger
    component<ecs::Faction>(b.r.fake, 1234).faction = static_cast<emf::gen::FactionType>(4);
    b.seconds(2);
    CHECK(b.faction(1234) == 4);  // never written back to ours
    CHECK_FALSE(contains(b.r.fake.destroyed, 1234));
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, at home") != std::string::npos);
}

TEST_CASE("beastmaster: a guest who leaves loses their beasts from the world but keeps them in the roster") {
    BeastRig b;
    write(b.data, "b 0x00000000000000bb wolf 0 1 Grey\n");
    b.spawnable(2);
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(2);
    REQUIRE(b.r.fake.spawns.size() == 1);
    remove_entity(b.r.fake, 2);  // the guest left
    b.seconds(2);
    CHECK(contains(b.r.fake.destroyed, 1234));
    CHECK(b.saved().find("b 0x00000000000000bb wolf 0 1 Grey") != std::string::npos);
    add_player(b.r.fake, 2, 0xBB, 30, 10, 0);  // back again
    add_entity(b.r.fake, 2);
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 2);
}

namespace {
size_t teleports_of(const Fake& f, uint32_t entity) {
    size_t n = 0;
    for (const auto& t : f.teleports)
        if (t.entity == entity) ++n;
    return n;
}
std::vector<std::pair<uint32_t, uint32_t>> attacks(BeastRig& b) {
    b.send_events();
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (const auto& [name, bytes] : b.r.fake.emitted) {
        if (name != "AddThreatTargetEvent") continue;
        const auto* e = reinterpret_cast<const ecs::AddThreatTargetEvent*>(bytes.data());
        out.push_back({e->attackerId.id, e->targetId.id});
    }
    return out;
}
// A wolf called out (entity 1234) for player 1.
void wolf_out(BeastRig& b) {
    b.reply("beastmaster.give wolf");
    b.spawnable(1);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
}
}  // namespace

TEST_CASE("beastmaster: a beast 50 m away for 3 s, or 90 m away at once, is teleported beside its owner") {
    BeastRig b;
    wolf_out(b);
    place(b.r.fake, 1234, 50, 10, 0);
    b.seconds(1);
    CHECK(teleports_of(b.r.fake, 1234) == 0);  // not yet: 3 s
    b.seconds(3);
    REQUIRE(teleports_of(b.r.fake, 1234) >= 1);
    const EmfTeleport& t = b.r.fake.teleports.back();
    CHECK(std::abs(t.x - 0.0) + std::abs(t.z - 0.0) <= 3.0);  // beside its owner at (0, 10, 0)
    CHECK((t.flags & EMF_TELEPORT_FIND_GROUND) != 0);
    place(b.r.fake, 1234, 2, 10, 0);  // it arrived
    b.seconds(4);
    const size_t before = b.r.fake.teleports.size();
    place(b.r.fake, 1234, 92, 10, 0);  // far at once
    b.seconds(1);
    CHECK(b.r.fake.teleports.size() == before + 1);
}

TEST_CASE("beastmaster: attack sends every beast out at the creature you look at") {
    BeastRig b;
    wolf_out(b);
    b.look(1, 0, 10.5, 0, 0);
    b.beast(60, "00000000-0000-0000-0000-000000000099", 0, 6, 4 /* Scavenger */, 100, 100);
    CHECK(b.reply("beastmaster.attack").find("attack") != std::string::npos);
    const auto a = attacks(b);
    REQUIRE(a.size() == 1);
    CHECK(a[0] == std::make_pair(1234u, 60u));
}

TEST_CASE("beastmaster: our beast's hit that leaves a foe at 20 % hands the killing blow to its owner, once") {
    BeastRig b;
    wolf_out(b);
    b.beast(60, "00000000-0000-0000-0000-000000000099", 0, 6, 4, 20, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1234, 60));
    deliver(b.r.fake, "HitEvent", hit_event(1234, 60));
    b.seconds(1);
    REQUIRE(b.r.fake.damages.size() == 1);
    CHECK(b.r.fake.damages[0] == std::make_tuple(1u, 60u, 20));
}

TEST_CASE("beastmaster: no credit for hits on players, allies, or foes above 25 %") {
    BeastRig b;
    wolf_out(b);
    b.beast(61, "00000000-0000-0000-0000-000000000099", 0, 6, kPlayerAlly, 10, 100);  // an ally
    b.beast(62, "00000000-0000-0000-0000-000000000099", 0, 7, 4, 50, 100);            // a foe at 50 %
    b.r.fake.attrs[{2, EMF_ATTR_HEALTH}] = 5;
    b.r.fake.attrs[{2, EMF_ATTR_MAX_HEALTH}] = 100;
    for (uint32_t target : {2u, 61u, 62u}) deliver(b.r.fake, "HitEvent", hit_event(1234, target));
    deliver(b.r.fake, "HitEvent", hit_event(99, 62));  // not our beast
    b.seconds(1);
    CHECK(b.r.fake.damages.empty());
    b.reply("beastmaster.give boar");  // the setting switches it off too
    b.r.fake.settings["beastmaster.xp_credit"] = "false";
    b.r.fake.attrs[{62, EMF_ATTR_HEALTH}] = 10;
    deliver(b.r.fake, "HitEvent", hit_event(1234, 62));
    b.seconds(1);
    CHECK(b.r.fake.damages.empty());
}

namespace {
std::map<uint32_t, bool> g_held;  // the fake keyboard for these tests: vk -> held
// Presses `vk` once on the server world (this game hosts) and lets go.
void press(BeastRig& b, uint32_t vk) {
    b.host->set_key_reader([](uint32_t k) { return g_held[k]; });
    g_held[vk] = true;
    b.host->tick(kServer);
    g_held[vk] = false;
    b.host->tick(kServer);
}
int index_of(const std::vector<std::string>& items, const std::string& start) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].rfind(start, 0) == 0) return static_cast<int>(i);
    return -1;
}
}  // namespace

TEST_CASE("beastmaster: the wheel lists the actions and up to 6 beasts to call; picking Call Wolf calls it") {
    BeastRig b;
    for (const char* s : {"wolf", "boar", "hound", "sabretooth", "wolf", "boar", "hound"}) b.reply(std::string("beastmaster.give ") + s);
    b.spawnable(1);
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    const auto& w = b.r.fake.wheels[0];
    CHECK(w.hold == VK_F3);
    for (const char* a : {"Tame", "Feed", "Attack"}) CHECK(index_of(w.items, a) >= 0);
    CHECK(index_of(w.items, "Dismiss") < 0);  // none out: nothing to dismiss
    int calls = 0;
    for (const auto& it : w.items) calls += it.rfind("Call ", 0) == 0;
    CHECK(calls == 6);  // 7 beasts at home: 6 on the wheel
    const int wolf = index_of(w.items, "Call Wolf");
    REQUIRE(wolf >= 0);
    w.cb(kClient, wolf, w.user);  // the wheel answers on the client world
    b.seconds(1);                 // the call runs on the server's next step
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kWolfTemplate);
    CHECK(b.noted("Wolf the wolf comes"));
    CHECK(b.hud("out").find("Wolf (out)") != std::string::npos);
}

TEST_CASE("beastmaster: each beast out has its own Dismiss item, Dismiss all comes with two out; /beastmaster dismiss alone sends all home") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    b.spawnable(2);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    b.seconds(2);
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    const auto w = b.r.fake.wheels[0];
    for (const char* a : {"Dismiss Wolf", "Dismiss Boar", "Dismiss all"}) CHECK_MESSAGE(index_of(w.items, a) >= 0, a);
    w.cb(kClient, index_of(w.items, "Dismiss Wolf"), w.user);
    b.seconds(1);
    CHECK(b.noted("Wolf goes home"));
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("Wolf - wolf, at home") != std::string::npos);
    CHECK(list.find("Boar - boar, out") != std::string::npos);
    press(b, VK_F3);  // one out: its own item, no Dismiss all
    REQUIRE(b.r.fake.wheels.size() == 2);
    CHECK(index_of(b.r.fake.wheels[1].items, "Dismiss Boar") >= 0);
    CHECK(index_of(b.r.fake.wheels[1].items, "Dismiss all") < 0);
    CHECK(index_of(b.r.fake.wheels[1].items, "Dismiss Wolf") < 0);
    CHECK(b.reply("beastmaster.dismiss").find("Boar goes home") != std::string::npos);  // no name: all
    CHECK(b.reply("beastmaster.dismiss all").find("No beast is out") != std::string::npos);
    // the help says how
    const std::string help = b.reply("beastmaster");
    CHECK(help.find("dismiss") != std::string::npos);
    CHECK(help.find("all") != std::string::npos);
    bool told = false;
    for (const auto& c : b.r.fake.commands)
        if (c.name == "beastmaster.dismiss") told = c.help.find("all") != std::string::npos && c.help.find("nothing") != std::string::npos;
    CHECK(told);
}

TEST_CASE("beastmaster: a guest's /beastmaster call acts on the guest's own beasts") {
    BeastRig b;
    b.reply("beastmaster.give wolf");  // the host's Wolf
    write(b.data, "b 0x00000000000000aa wolf 0 0 Wolf\nb 0x00000000000000bb boar 0 0 Grunt\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    b.spawnable(1);
    CHECK(b.reply("beastmaster.call Wolf", 2, 0xBB).find("No such beast") != std::string::npos);  // not theirs
    CHECK(b.reply("beastmaster.call Grunt", 2, 0xBB).find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(std::abs(b.r.fake.spawns[0].x - 30.0) <= 3.0);  // beside the guest at (30, 10, 0)
    CHECK(b.added(2, EMF_ATTR_MANA, -15));
}

namespace {
constexpr const char* kBearTemplate = "b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f02";
constexpr const char* kAurochsTemplate = "d5f2b8e3-7c4a-4e9f-8b21-6a3c8d4e0f02";  // the beasts pack's
constexpr const char* kMinotaurTemplate = "69120584-30ec-43e5-bd54-cce04295cdb6";  // the beasts pack's
size_t logged(const Fake& f, const std::string& needle) {
    size_t n = 0;
    for (const auto& l : f.log) n += l.find(needle) != std::string::npos;
    return n;
}
// The beasts pack's rare snakes (a wild snake is one now and then): missing, for the tests of the plain picks.
void no_rare_snakes(Fake& f) {
    for (const char* t : {"d4c87337-8c53-41df-8488-8079046e1512", "d4c87337-8c53-41df-8488-8079046e1522",
                          "d4c87337-8c53-41df-8488-8079046e1532"})
        f.missing_templates.insert(guid_bytes(t));
}
}  // namespace

TEST_CASE("wild bears: one spawns 50-70 m from a player when none exists; none when the pack is missing") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));  // only the bear pack: the bear every time
    b.r.fake.missing_templates.insert(guid_bytes("d4c87337-8c53-41df-8488-8079046e1502"));  // (no beasts pack snake)
    b.r.fake.missing_templates.insert(guid_bytes(kMinotaurTemplate));                         // (nor minotaur)
    no_rare_snakes(b.r.fake);
    b.spawnable(3);
    b.seconds(30);
    CHECK(b.r.fake.spawns.empty());  // not before a minute of play
    b.seconds(31);
    REQUIRE(b.r.fake.spawns.size() == 1);
    component<ecs::ScaleComponent>(b.r.fake, 1234).uniformScale = 1.4f;  // the pack's bear (seen in game: 1.40)
    const auto s = b.r.fake.spawns[0];
    CHECK(guid_text(s.guid) == kBearTemplate);
    CHECK(s.y == doctest::Approx(12.0));  // the player's height + 2 m
    const double d1 = std::hypot(s.x - 0.0, s.z - 0.0), d2 = std::hypot(s.x - 30.0, s.z - 0.0);
    CHECK(((d1 >= 50.0 && d1 <= 70.0) || (d2 >= 50.0 && d2 <= 70.0)));
    CHECK(teleports_of(b.r.fake, 1234) == 0);  // not at once: the game refuses a creature that is not set up yet
    b.seconds(2);
    REQUIRE(teleports_of(b.r.fake, 1234) == 1);  // then onto the ground there
    CHECK((b.r.fake.teleports.back().flags & EMF_TELEPORT_FIND_GROUND) != 0);
    CHECK(b.faction(1234) == kWildBeast);  // wild: tamed like any beast
    CHECK(b.r.fake.components[{ecs::Level::kHash, 1234}][8] == 10);  // at the player's level
    b.seconds(60);
    CHECK(b.r.fake.spawns.size() == 1);  // one at a time
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 0;  // killed
    b.seconds(61);
    CHECK(b.r.fake.spawns.size() == 2);

    // without the bear pack the game has no bear: wild bears switch off, with one log line
    BeastRig m({{"wild_bear_minutes", "1"}});
    m.r.fake.spawn_fails = true;
    m.seconds(200);
    CHECK(m.r.fake.spawns.empty());
    CHECK(logged(m.r.fake, "wild bears") == 1);

    // and the setting switches them off
    BeastRig off({{"wild_bears", "false"}, {"wild_bear_minutes", "1"}});
    off.spawnable(1);
    off.seconds(200);
    CHECK(off.r.fake.spawns.empty());
}

// Health's attribute storage (seen in game): [0] health, [1] min, [2] Health_Max_Base (the game sets it from the level),
// [3] Health_Max_Adder, ..., [7] the maximum (base + adder, computed by the game).
TEST_CASE("beastmaster: a bear's health adder keeps its maximum at 2.5x the game's base, also after a level change; a wolf keeps its own") {
    BeastRig b;
    b.reply("beastmaster.give bear");
    b.reply("beastmaster.give wolf");
    b.spawnable(2);
    auto& bear = component<ecs::Health>(b.r.fake, 1234);
    auto& wolf = component<ecs::Health>(b.r.fake, 1235);
    CHECK(b.reply("beastmaster.call Bear").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(bear.dataStorage[3] == 0);  // no base yet: nothing to multiply
    bear.dataStorage[2] = 34;         // the game's base at level 1 (the earlygame wolf's)
    b.seconds(1);
    CHECK(bear.dataStorage[3] == 51);  // 34 + 51 = 85 = 2.5 x 34
    bear.dataStorage[2] = 65;          // the game rescaled it to a new level
    b.seconds(2);
    CHECK(bear.dataStorage[3] == 98);  // 65 x 1.5 = 97.5
    wolf.dataStorage[2] = 34;
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(wolf.dataStorage[3] == 0);
}

TEST_CASE("beastmaster: /beastmaster <word> in the chat runs the players' commands; no word or an unknown one lists them") {
    BeastRig b;
    write(b.data, "b 0x00000000000000bb boar 0 0 Grunt\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    b.spawnable(1);
    CHECK(b.reply("beastmaster list", 2, 0xBB).find("1. Grunt - boar") != std::string::npos);
    CHECK(b.reply("beastmaster call Grunt", 2, 0xBB).find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(std::abs(b.r.fake.spawns[0].x - 30.0) <= 3.0);  // beside the guest
    for (const char* asked : {"beastmaster", "beastmaster fly"}) {
        const std::string help = b.reply(asked, 2, 0xBB);
        for (const char* word : {"list", "tame", "feed", "call", "dismiss", "attack"})
            CHECK_MESSAGE(help.find(word) != std::string::npos, asked, " ", word);
        CHECK(help.find("give") == std::string::npos);  // the host's console only
    }
}

TEST_CASE("beastmaster: with nothing under the crosshair, tame takes the nearest weakened beast within 8 m and feed the nearest beast") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    b.look(1, 0, 10.5, 0, 180);                              // looking away from them all (-z)
    b.beast(50, kWolfGraph, 3, 1, kWildBeast, 100, 100);    // beside you, healthy
    b.beast(51, kWolfGraph, -4, 2, kWildBeast, 20, 100);    // beside you, beaten down
    b.beast(52, kWolfGraph, 0, 12, kWildBeast, 10, 100);    // beaten down, but 12 m away
    CHECK(b.reply("beastmaster.tame").find("Taming the wolf") != std::string::npos);
    b.seconds(4);
    CHECK(b.faction(51) == kPlayerAlly);
    CHECK(b.faction(50) == kWildBeast);
    CHECK(b.faction(52) == kWildBeast);
    CHECK(b.reply("beastmaster.feed").find("You offer the wolf") != std::string::npos);  // the healthy one beside you
    REQUIRE(b.r.fake.takes.size() == 1);
    b.r.fake.attrs[{50, EMF_ATTR_HEALTH}] = 100;
    place(b.r.fake, 50, 0, 10, 20);                          // gone far away: nothing near to tame
    b.seconds(11);
    CHECK(b.reply("beastmaster.tame").find("no beast to tame") != std::string::npos);
}

TEST_CASE("beastmaster: attack with nothing under the crosshair goes for the creature that hit you in the last 10 s") {
    BeastRig b;
    wolf_out(b);
    b.look(1, 0, 10.5, 0, 180);
    b.beast(60, "00000000-0000-0000-0000-000000000099", 0, 6, 4 /* Scavenger */, 100, 100);
    CHECK(b.reply("beastmaster.attack").find("Look at a creature") != std::string::npos);  // nothing hit you yet
    deliver(b.r.fake, "HitEvent", hit_event(60, 1));
    b.seconds(1);
    CHECK(b.reply("beastmaster.attack").find("Your beasts attack") != std::string::npos);
    const auto a = attacks(b);
    REQUIRE(a.size() == 1);
    CHECK(a[0] == std::make_pair(1234u, 60u));
    b.seconds(11);
    CHECK(b.reply("beastmaster.attack").find("Look at a creature") != std::string::npos);  // forgotten after 10 s
}

TEST_CASE("beastmaster: a bear is the wolf's graph at 1.4x scale (the pack's ScaleComponent); a wolf, or a reused id, is a wolf") {
    BeastRig b;
    b.beast(1234, kWolfGraph, 2, 2, kWildBeast, 20, 100);  // a wild bear: it came to you, and you beat it down
    component<ecs::ScaleComponent>(b.r.fake, 1234).uniformScale = 1.4f;
    b.look(1, 0, 10.5, 0, 45);
    CHECK(b.reply("beastmaster.tame").find("Taming the bear") != std::string::npos);
    b.seconds(4);
    CHECK(b.reply("beastmaster.list").find("1. Bear - bear, out") != std::string::npos);
    b.beast(70, kWolfGraph, -2, 2, kWildBeast, 20, 100);  // a game wolf: still a wolf
    CHECK(b.reply("beastmaster.tame").find("Taming the wolf") != std::string::npos);
}

TEST_CASE("beastmaster: the bear pack 2's bear is its own graph (its own skeleton) at 1.4x; the older pack's is the wolf's") {
    constexpr const char* kBearGraph = "b3a7c2e1-5f4d-4c8b-9e21-7a0d4b6e2f05";
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kBearGraph + " 6 1.4") == "bear");
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6 1.4") == "bear");  // the bear pack 1.0
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6 1") == "wolf");
    b.beast(60, kBearGraph, 2, 2, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 60).uniformScale = 1.4f;
    b.look(1, 0, 10.5, 0, 45);
    CHECK(b.reply("beastmaster.tame").find("Taming the bear") != std::string::npos);
    b.seconds(4);
    CHECK(b.reply("beastmaster.list").find("1. Bear - bear, out") != std::string::npos);
}

TEST_CASE("beastmaster: a creature that gets a dead wild bear's id is not a bear") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.spawnable(1);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));  // the wild bear dies; the game gives its id to a wolf
    b.beast(1234, kWolfGraph, 2, 2, kWildBeast, 20, 100);
    b.look(1, 0, 10.5, 0, 45);
    CHECK(b.reply("beastmaster.tame").find("Taming the wolf") != std::string::npos);
}

TEST_CASE("beastmaster: a calmed beast that is out of range for under 2 s (it was fleeing) is still tamed; 2 s breaks it") {
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    place(b.r.fake, 50, 0, 10, 10);  // it runs on for a moment: 10 m
    b.seconds(1);
    place(b.r.fake, 50, 0, 10, 2);   // and comes to you
    b.seconds(3);
    CHECK(b.noted("You tamed a wolf"));
    CHECK(!b.noted("too far"));
    b.beast(51, kWolfGraph, 0, 4, kWildBeast, 20, 100);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    place(b.r.fake, 51, 0, 10, 12);
    b.seconds(3);
    CHECK(b.noted("too far"));
    CHECK(b.faction(51) == kWildBeast);
}

// ---- edge cases ----

TEST_CASE("beastmaster: no XP credit (no killing blow) on bosses, mini-bosses or the player side") {
    BeastRig b;
    wolf_out(b);
    b.beast(62, "00000000-0000-0000-0000-000000000099", 0, 7, 3 /* Vukah */, 20, 100);  // a boss at 20 %
    component<ecs::Boss>(b.r.fake, 62);
    b.beast(63, "00000000-0000-0000-0000-000000000099", 0, 8, 3, 20, 100);  // a mini-boss (its boss bar)
    component<ecs::BossHealthBar>(b.r.fake, 63);
    b.beast(64, "00000000-0000-0000-0000-000000000099", 0, 9, 7 /* Player */, 20, 100);
    b.beast(65, "00000000-0000-0000-0000-000000000099", 0, 10, 8 /* AutomatedPlayer */, 20, 100);
    for (uint32_t target : {62u, 63u, 64u, 65u}) deliver(b.r.fake, "HitEvent", hit_event(1234, target));
    b.seconds(1);
    CHECK(b.r.fake.damages.empty());
}

TEST_CASE("beastmaster: a second tame or feed during a channel turns the first calmed beast wild again") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);   // under the crosshair
    b.beast(51, kWolfGraph, 3, -2, kWildBeast, 20, 100);  // beside you
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    CHECK(b.faction(50) == kPlayerAlly);  // calmed
    b.look(1, 0, 10.5, 0, 180);           // you turn: 50 is no longer under the crosshair; 51 is the nearest wild one
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    CHECK(b.faction(51) == kPlayerAlly);
    CHECK(b.faction(50) == kWildBeast);   // not left a stray ally
    CHECK(b.reply("beastmaster.feed").find("You offer the wolf") != std::string::npos);  // 50, wild again and nearest
    CHECK(b.faction(51) == kWildBeast);   // one channel at a time: feeding ends the taming of 51
}

TEST_CASE("beastmaster: a new world drops the old world's taming channel, and the HUD keeps up when the game clock restarts") {
    BeastRig b;
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    b.reply("beastmaster.give boar");
    b.spawnable(1);
    CHECK(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(b.hud("out").find("Boar (out)") != std::string::npos);
    b.r.fake.session = 2;  // another world: entity 50 is someone else there, and the clock starts again
    b.r.fake.tick = 0;
    component<ecs::Faction>(b.r.fake, 50).faction = static_cast<emf::gen::FactionType>(kWildBeast);
    fire_load(1);
    b.seconds(4);
    CHECK(b.faction(50) == kWildBeast);  // not tamed, not touched
    CHECK(!b.noted("You tamed"));
    b.reply("beastmaster.dismiss all");
    b.seconds(2);
    CHECK(b.hud("out").find("Boar (out)") == std::string::npos);
}

TEST_CASE("beastmaster: a remembered id that now guards someone else is not ours any more; a body is removed only while dead") {
    BeastRig b;
    wolf_out(b);
    b.seconds(1);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234).guardEntityId.id = 999;  // another entity's guard now
    b.seconds(2);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, out") != std::string::npos);  // not at the first look
    b.seconds(2);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, at home") != std::string::npos);
    b.r.fake.destroyed.clear();
    b.reply("beastmaster.dismiss all");
    CHECK(!contains(b.r.fake.destroyed, 1234));
    // a beast falls; before its body goes, the game gives the id to a living creature
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    deliver(b.r.fake, "EntityDiedEvent", died_event(1235));
    b.r.fake.attrs[{1235, EMF_ATTR_HEALTH}] = 100;  // alive: another creature
    b.seconds(3);
    CHECK(!contains(b.r.fake.destroyed, 1235));
}

TEST_CASE("beastmaster: an unknown bait name gives a note and the mod keeps running") {
    BeastRig b({{"bait_wolf", "No_Such_Item"}});
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 100, 100);
    CHECK(b.reply("beastmaster.feed").find("No_Such_Item") != std::string::npos);
    CHECK(b.host->status(b.id) == "running");
    CHECK(b.reply("beastmaster.list").find("no beasts") != std::string::npos);
}

TEST_CASE("wild bears: none near a base (40 m of a flame altar), neither by a player there nor at the spot") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.spawnable(2);
    place(b.r.fake, 900, 15, 10, 0);  // a flame altar between the two players
    component<ecs::BaseIdComponent>(b.r.fake, 900);
    add_entity(b.r.fake, 900);
    b.seconds(130);
    CHECK(b.r.fake.spawns.empty());  // both players are at the base
    place(b.r.fake, 1, 0, 10, -300);  // player 1 goes out: a bear may come, but never within 40 m of the altar
    place(b.r.fake, 2, 30, 10, -300);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    const auto s = b.r.fake.spawns[0];
    CHECK(std::hypot(s.x - 15.0, s.z - 0.0) > 40.0);
}

TEST_CASE("beastmaster: a wheel pick calls the beast that was shown, even when the list moved meanwhile (shared and own wheel)") {
    for (const char* shared : {"true", "false"}) {
        BeastRig b({{"shared_wheel", shared}});
        b.reply("beastmaster.give wolf");
        b.reply("beastmaster.give boar");
        b.spawnable(2);
        b.seconds(2);
        press(b, VK_F3);
        REQUIRE(b.r.fake.wheels.size() == 1);
        const auto w = b.r.fake.wheels[0];
        const int boar = index_of(w.items, "Call Boar");
        REQUIRE(boar >= 0);
        CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);  // meanwhile: the list moves up
        b.seconds(11);                                                                 // past the call cooldown
        w.cb(kClient, boar, w.user);
        b.seconds(1);
        REQUIRE_MESSAGE(b.r.fake.spawns.size() == 2, shared);
        CHECK_MESSAGE(guid_text(b.r.fake.spawns[1].guid) == "255fd8d9-4447-445d-8ffc-4d9dc0afdd16", shared);  // the boar
    }
}

// ---- new beasts (the beasts pack): the Aurochs ----
namespace {
constexpr const char* kBoarGraph = "aed0ecec-8a98-4fd3-a450-ab4c508b0ad4";
}  // namespace

TEST_CASE("beastmaster: an aurochs is the boar graph at 1.2x or more (WildBeast); a game boar or a scaled hound pet is not") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kBoarGraph + " 6 1.3") == "aurochs");
    CHECK(b.reply(std::string("beastmaster.species ") + kBoarGraph + " 6") == "boar");
    CHECK(b.reply(std::string("beastmaster.species ") + kBoarGraph + " 3 1.3") == "unknown");  // a Vukah's big boar
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 4 1.25") == "hound");     // the scavengers' pet
    b.beast(50, kBoarGraph, 0, 5, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.3f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the aurochs") != std::string::npos);
    b.seconds(4);
    CHECK(b.reply("beastmaster.list").find("1. Aurochs - aurochs, out") != std::string::npos);
    // called: the pack's template, with 3x health
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Aurochs").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kAurochsTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 80);  // 40 + 80 = 120 = 3 x 40
}

TEST_CASE("beastmaster: a beast whose pack is missing goes home with one note; it is not called again every second") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa aurochs 0 1 Aurochs\n");  // saved as out
    b.r.fake.session = 2;
    b.r.fake.spawn_fails = true;  // this game has no aurochs (the beasts pack is not installed)
    fire_load(1);
    b.seconds(5);
    size_t notes = 0;
    for (const auto& n : b.r.fake.notes) notes += n.find("Aurochs") != std::string::npos;
    CHECK(notes == 1);
    CHECK(b.reply("beastmaster.list").find("Aurochs - aurochs, at home") != std::string::npos);
    CHECK(b.reply("beastmaster.call Aurochs").find("pack") != std::string::npos);
}

TEST_CASE("beastmaster: a pack beast that cannot come says it needs the EMF creature packs, which load through EML or ShroudForge") {
    BeastRig b;
    b.reply("beastmaster.give bear");
    b.r.fake.spawn_fails = true;  // no bear in this game: no pack, or no EML to load it (the mod cannot tell which)
    const std::string call = b.reply("beastmaster.call Bear");
    CHECK(call.find("EMF creature packs") != std::string::npos);
    CHECK(call.find("EML or ShroudForge") != std::string::npos);
    CHECK(call.find("Install EML first") != std::string::npos);
    CHECK(call.find("emf_beastmaster") == std::string::npos);  // no folder names
    write(b.data, "b 0x00000000000000aa bear 0 1 Bear\n");  // saved as out
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(5);
    std::string note;
    for (const auto& n : b.r.fake.notes)
        if (n.find("Bear") != std::string::npos) note = n;
    CHECK(note.find("stays home") != std::string::npos);
    CHECK(note.find("EMF creature packs") != std::string::npos);
    CHECK(note.find("EML or ShroudForge") != std::string::npos);
}

TEST_CASE("wild beasts: with only the beasts pack installed, wild aurochs, snakes and minotaurs come (the missing bear is skipped)") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    no_rare_snakes(b.r.fake);
    b.spawnable(8);
    for (int round = 0; round < 4; ++round) {
        const size_t before = b.r.fake.spawns.size();
        b.seconds(61);
        REQUIRE(b.r.fake.spawns.size() == before + 1);
        const std::string spawned = guid_text(b.r.fake.spawns.back().guid);
        const bool aurochs = spawned == kAurochsTemplate;
        CHECK((aurochs || spawned == "d4c87337-8c53-41df-8488-8079046e1502" || spawned == kMinotaurTemplate));
        const uint32_t id = 1234 + static_cast<uint32_t>(before);
        component<ecs::ScaleComponent>(b.r.fake, id).uniformScale = aurochs ? 1.3f : 1.0f;  // the pack's sizes
        CHECK(b.faction(id) == kWildBeast);
        b.seconds(60);
        CHECK(b.r.fake.spawns.size() == before + 1);  // one at a time: it is still about
        b.r.fake.attrs[{id, EMF_ATTR_HEALTH}] = 0;     // killed
    }
    CHECK(logged(b.r.fake, "wild bears") <= 1);
}

// ---- the beasts pack's snake: a skeleton of its own, played by its own copy of the wolf's graph ----
namespace {
constexpr const char* kSnakeGraph = "d4c87337-8c53-41df-8488-8079046e1505";
constexpr const char* kSnakeTemplate = "d4c87337-8c53-41df-8488-8079046e1502";
}  // namespace

TEST_CASE("beastmaster: a snake is its own graph (WildBeast): tamed, listed, called with the pack's template and 2x health") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kSnakeGraph + " 6 1") == "snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kSnakeGraph + " 6") == "snake");
    b.beast(50, kSnakeGraph, 0, 5, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.0f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the snake") != std::string::npos);
    b.seconds(4);
    CHECK(b.reply("beastmaster.list").find("1. Snake - snake, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Snake").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kSnakeTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 40);  // 40 + 40 = 80 = 2 x 40
    CHECK(b.reply("beastmaster.give snake").find("snake") != std::string::npos);
}

TEST_CASE("wild beasts: wild snakes come with the beasts pack, one at a time") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kMinotaurTemplate));
    no_rare_snakes(b.r.fake);
    b.spawnable(8);
    for (int round = 0; round < 3; ++round) {
        const size_t before = b.r.fake.spawns.size();
        b.seconds(61);
        REQUIRE(b.r.fake.spawns.size() == before + 1);
        CHECK(guid_text(b.r.fake.spawns.back().guid) == kSnakeTemplate);
        const uint32_t id = 1234 + static_cast<uint32_t>(before);
        component<ecs::ScaleComponent>(b.r.fake, id).uniformScale = 1.0f;  // the pack's snake
        CHECK(b.faction(id) == kWildBeast);
        b.seconds(60);
        CHECK(b.r.fake.spawns.size() == before + 1);  // one at a time: it is still about
        b.r.fake.attrs[{id, EMF_ATTR_HEALTH}] = 0;     // killed
    }
}

// ---- the beasts pack's minotaur: a skeleton of its own, played by its own copy of the armoured Vukah elite's graph ----
namespace {
constexpr const char* kMinotaurGraph = "e0a47a97-379b-4384-8602-79eeeffc4a92";
constexpr int kVukahFaction = 3;
}  // namespace

TEST_CASE("beastmaster: a minotaur is its own graph (Vukah): tamed with game meat, listed, called with the pack's template and the game's own health") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kMinotaurGraph + " 3 1") == "minotaur");
    CHECK(b.reply(std::string("beastmaster.species ") + kMinotaurGraph + " 6 1") == "unknown");  // not its faction
    CHECK(b.r.fake.settings["beastmaster.bait_minotaur"] == "Food_T2_raw_bad_meat_Game");
    b.beast(50, kMinotaurGraph, 0, 5, kVukahFaction, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.0f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the minotaur") != std::string::npos);
    b.seconds(4);
    CHECK(b.noted("You tamed a minotaur: Minotaur"));
    CHECK(b.reply("beastmaster.list").find("1. Minotaur - minotaur, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Minotaur").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kMinotaurTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 239;  // the armoured Vukah elite's base health at level 1
    b.seconds(1);
    CHECK(h.dataStorage[3] == 0);  // no more: an elite is tough already
    CHECK(b.reply("beastmaster.give minotaur").find("minotaur") != std::string::npos);
}

TEST_CASE("wild beasts: a wild minotaur comes with the beasts pack, one at a time") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    for (const char* t : {kBearTemplate, kAurochsTemplate, kSnakeTemplate}) b.r.fake.missing_templates.insert(guid_bytes(t));
    no_rare_snakes(b.r.fake);
    b.spawnable(4);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kMinotaurTemplate);
    component<ecs::ScaleComponent>(b.r.fake, 1234).uniformScale = 1.0f;  // the template's own (seen in game: 1.00)
    b.seconds(60);
    CHECK(b.r.fake.spawns.size() == 1);  // still about
}

// ---- the beasts pack's rare snakes: crimson, golden king and albino, each its own copy of the snake's graph ----
namespace {
constexpr const char* kCrimsonGraph = "d4c87337-8c53-41df-8488-8079046e1515";
constexpr const char* kCrimsonTemplate = "d4c87337-8c53-41df-8488-8079046e1512";
constexpr const char* kGoldenGraph = "d4c87337-8c53-41df-8488-8079046e1525";
constexpr const char* kGoldenTemplate = "d4c87337-8c53-41df-8488-8079046e1522";
constexpr const char* kAlbinoGraph = "d4c87337-8c53-41df-8488-8079046e1535";
constexpr const char* kAlbinoTemplate = "d4c87337-8c53-41df-8488-8079046e1532";
constexpr uint32_t kRune = 0x5e000001, kRareRune = 0x5e000002, kEpicSword = 0x5e000003;

// Beastmaster with every math.random answer fixed by `beastmaster.test_roll <r>`: math.random() is r, math.random(m, n)
// is m below 0.5 and n from 0.5 (math.random(n) as math.random(1, n)).
struct RollRig : BeastRig {
    explicit RollRig(const std::map<std::string, std::string>& settings = {}) : BeastRig(settings) {
        r.fake.item_names[kRune] = "Material_T1_Token_WeaponUpgrade_Rune01";
        r.fake.item_names[kRareRune] = "Material_T2_Token_WeaponUpgrade_Rune02";
        r.fake.item_names[kEpicSword] = "Weapon_T1_1H_SwordEpic_01";
        edit_main("local wild_mod = require(\"wild\")\n",
                  "TEST_ROLL = 0.5\n"
                  "math.random = function(m, n)\n"
                  "  if m == nil then return TEST_ROLL end\n"
                  "  if n == nil then m, n = 1, m end\n"
                  "  if TEST_ROLL < 0.5 then return m end\n"
                  "  return n\n"
                  "end\n"
                  "emf.command(\"test_roll\", \"<r>\", function(w, a) TEST_ROLL = tonumber(a[1]) end)\n");
    }
    void roll(double v) { reply("beastmaster.test_roll " + std::to_string(v)); }
    // The puts made into `entity`'s inventory: item -> (count, level, rarity or -1)
    std::map<uint32_t, std::tuple<uint32_t, uint32_t, int>> puts_to(uint32_t entity) {
        std::map<uint32_t, std::tuple<uint32_t, uint32_t, int>> out;
        for (const auto& p : r.fake.puts)
            if (p.entity == entity)
                out[p.item] = {p.count, p.level, (p.flags & EMF_ITEMS_RARITY) ? static_cast<int>(p.rarity) : -1};
        return out;
    }
};
}  // namespace

TEST_CASE("beastmaster: the rare snakes are their own graphs: tamed with 3x, 4x and 5x health, fed game meat or dog bait") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kCrimsonGraph + " 6 1.15") == "crimson_snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kGoldenGraph + " 6 1.25") == "golden_snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kAlbinoGraph + " 6 1.35") == "albino_snake");
    CHECK(b.r.fake.settings["beastmaster.bait_crimson_snake"] == "Food_T2_raw_bad_meat_Game");
    CHECK(b.r.fake.settings["beastmaster.bait_golden_snake"] == "Food_T2_raw_bad_meat_Game");
    CHECK(b.r.fake.settings["beastmaster.bait_albino_snake"] == "Food_T2_Dog_Bait");
    b.beast(50, kAlbinoGraph, 0, 5, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.35f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the albino snake") != std::string::npos);
    b.seconds(4);
    CHECK(b.noted("You tamed an albino snake: Albino snake"));
    CHECK(b.reply("beastmaster.list").find("1. Albino snake - albino_snake, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call 1").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kAlbinoTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 160);  // 40 + 160 = 200 = 5 x 40
    for (const char* id : {"crimson_snake", "golden_snake"}) CHECK(b.reply(std::string("beastmaster.give ") + id).find(id) != std::string::npos);
}

TEST_CASE("wild beasts: a wild snake is now and then a rare one: albino 0.25 %, golden king 0.4 %, crimson 1 % of them") {
    RollRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kMinotaurTemplate));
    b.spawnable(8);
    // the roll in [0, 1): albino under 0.0025, golden king under 0.0065, crimson under 0.0165, else a snake
    const std::vector<std::pair<double, const char*>> rounds = {
        {0.001, kAlbinoTemplate}, {0.005, kGoldenTemplate}, {0.012, kCrimsonTemplate}, {0.02, kSnakeTemplate},
        {0.3, kSnakeTemplate}};
    for (const auto& [roll, want] : rounds) {
        CAPTURE(roll);
        b.roll(roll);
        const size_t before = b.r.fake.spawns.size();
        b.seconds(61);
        REQUIRE(b.r.fake.spawns.size() == before + 1);
        CHECK(guid_text(b.r.fake.spawns.back().guid) == want);
        const uint32_t id = 1234 + static_cast<uint32_t>(before);
        component<ecs::ScaleComponent>(b.r.fake, id).uniformScale = 1.0f;
        b.r.fake.attrs[{id, EMF_ATTR_HEALTH}] = 0;  // killed: the next one may come
    }

    // a rare snake the game does not have (an older beasts pack) comes as a snake, with one log line
    RollRig old({{"wild_bear_minutes", "1"}});
    for (const char* t : {kBearTemplate, kAurochsTemplate, kMinotaurTemplate, kCrimsonTemplate})
        old.r.fake.missing_templates.insert(guid_bytes(t));
    old.spawnable(4);
    old.roll(0.012);
    old.seconds(61);
    REQUIRE(old.r.fake.spawns.size() == 1);
    CHECK(guid_text(old.r.fake.spawns[0].guid) == kSnakeTemplate);
    CHECK(logged(old.r.fake, "wild crimson snakes") == 1);
}

TEST_CASE("wild beasts: a wild albino snake keeps 5x the game's base health") {
    RollRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kMinotaurTemplate));
    b.spawnable(1);
    b.roll(0.001);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kAlbinoTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 30;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 120);  // 30 + 120 = 150 = 5 x 30
}

TEST_CASE("rare snakes: the slayer gets runes, maybe the tier-2 rune and gear at their level; nobody far away does") {
    RollRig b;
    // an albino, lucky rolls: the fewest runes (6), the tier-2 rune (50 %) and legendary gear (25 %)
    b.roll(0.001);
    b.beast(70, kAlbinoGraph, 0, 5, kWildBeast, 0, 100);
    component<ecs::ScaleComponent>(b.r.fake, 70).uniformScale = 1.35f;
    deliver(b.r.fake, "HitEvent", hit_event(1, 70));
    deliver(b.r.fake, "EntityDiedEvent", died_event(70));
    b.seconds(1);
    auto got = b.puts_to(1);
    REQUIRE(got.count(kRune) == 1);
    CHECK(std::get<0>(got[kRune]) == 6);
    REQUIRE(got.count(kRareRune) == 1);
    CHECK(std::get<0>(got[kRareRune]) == 1);
    REQUIRE(got.count(kEpicSword) == 1);
    CHECK(std::get<0>(got[kEpicSword]) == 1);
    CHECK(std::get<1>(got[kEpicSword]) == 10);  // the slayer's level
    CHECK(std::get<2>(got[kEpicSword]) == 4);   // legendary
    CHECK(b.puts_to(2).empty());

    // a crimson, unlucky rolls: the most runes (5), nothing else; player 2 hit it last
    b.r.fake.puts.clear();
    b.roll(0.99);
    b.beast(71, kCrimsonGraph, 30, 5, kWildBeast, 0, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1, 71));
    deliver(b.r.fake, "HitEvent", hit_event(2, 71));
    deliver(b.r.fake, "EntityDiedEvent", died_event(71));
    b.seconds(1);
    got = b.puts_to(2);
    REQUIRE(got.size() == 1);
    CHECK(std::get<0>(got[kRune]) == 5);
    CHECK(b.puts_to(1).empty());

    // a golden king with lucky rolls: epic gear (10 %), runes 4
    b.r.fake.puts.clear();
    b.roll(0.001);
    b.beast(72, kGoldenGraph, 0, 5, kWildBeast, 0, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1, 72));
    deliver(b.r.fake, "EntityDiedEvent", died_event(72));
    b.seconds(1);
    got = b.puts_to(1);
    CHECK(std::get<0>(got[kRune]) == 4);
    CHECK(std::get<2>(got[kEpicSword]) == 3);  // epic

    // no player's hit (a beast or a fall killed it): the nearest player within 60 m
    b.r.fake.puts.clear();
    b.beast(73, kCrimsonGraph, 28, 0, kWildBeast, 0, 100);
    deliver(b.r.fake, "EntityDiedEvent", died_event(73));
    b.seconds(1);
    CHECK(b.puts_to(2).count(kRune) == 1);
    CHECK(b.puts_to(1).empty());

    // far from every player and no player's hit: no bonus; nor for a tamed one, a plain snake, or a wolf
    b.r.fake.puts.clear();
    b.beast(74, kCrimsonGraph, 200, 200, kWildBeast, 0, 100);
    deliver(b.r.fake, "EntityDiedEvent", died_event(74));
    b.beast(75, kCrimsonGraph, 0, 5, kPlayerAlly, 0, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1, 75));
    deliver(b.r.fake, "EntityDiedEvent", died_event(75));
    b.beast(76, kSnakeGraph, 0, 5, kWildBeast, 0, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1, 76));
    deliver(b.r.fake, "EntityDiedEvent", died_event(76));
    b.beast(77, kWolfGraph, 0, 5, kWildBeast, 0, 100);
    deliver(b.r.fake, "HitEvent", hit_event(1, 77));
    deliver(b.r.fake, "EntityDiedEvent", died_event(77));
    b.seconds(1);
    CHECK(b.r.fake.puts.empty());
}

// ---- the sabretooth graph: no pack beast on it ----
namespace {
constexpr const char* kSabretoothGraph = "9c1b120e-4901-495c-9cc0-5de134fbe140";
}

TEST_CASE("beastmaster: no panther: a small sabretooth is a sabretooth, give panther is refused") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kSabretoothGraph + " 6 0.85") == "sabretooth");
    CHECK(b.reply("beastmaster.give panther").find("no species panther") != std::string::npos);
    CHECK(b.reply("beastmaster.give panther").find("aurochs") != std::string::npos);  // the list names every species
}

TEST_CASE("beastmaster: a panther saved by Beastmaster 1.1 loads as a sabretooth (it keeps its name and slot)") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa panther 0 0 Shadow\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("Shadow - sabretooth") != std::string::npos);
    CHECK(list.find("panther") == std::string::npos);
}

TEST_CASE("beastmaster: an aurochs that was out comes back after its first call-backs fail for a moment (no position yet, one refused spawn)") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa aurochs 0 1 Aurochs\n");
    b.r.fake.session = 2;
    b.r.fake.components.erase(std::make_pair(ecs::CurrentTransform::kHash, 1u));  // the player is not placed yet
    fire_load(1);
    b.seconds(2);
    place(b.r.fake, 1, 0, 10, 0);
    b.r.fake.spawn_fails = true;  // then the game refuses one spawn
    b.seconds(1);
    b.r.fake.spawn_fails = false;
    b.spawnable(1);
    b.seconds(2);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kAurochsTemplate);
    CHECK(b.reply("beastmaster.list").find("Aurochs - aurochs, out") != std::string::npos);
    size_t notes = 0;
    for (const auto& x : b.r.fake.notes) notes += x.find("Aurochs") != std::string::npos;
    CHECK(notes == 0);
}

// ---- holding a beast during the taming channel (a beaten beast that flees runs out of range) ----
namespace {
// Movement's attribute storage: [0] movement, [1] the multiplier (1 normally), [2..] speeds
float move_mul(Fake& f, uint32_t id) {  // read as it is (component<> would reset it)
    const auto& v = f.components[{ecs::Movement::kHash, id}];
    return v.size() < sizeof(ecs::Movement) ? -1.0f : reinterpret_cast<const ecs::Movement*>(v.data())->dataStorage[1];
}
void set_move_mul(Fake& f, uint32_t id, float m) { component<ecs::Movement>(f, id).dataStorage[1] = m; }
}  // namespace

TEST_CASE("beastmaster: a beast being tamed is held: its movement multiplier is 0 for the channel and back after") {
    BeastRig b;
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    set_move_mul(b.r.fake, 50, 1.0f);
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    CHECK(move_mul(b.r.fake, 50) == 0.0f);
    b.seconds(4);
    CHECK(b.noted("You tamed a wolf"));
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
}

TEST_CASE("beastmaster: a held beast is let go when the channel breaks or another one starts") {
    BeastRig b;
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    set_move_mul(b.r.fake, 50, 1.0f);
    b.look(1, 0, 10.5, 0, 0);
    b.reply("beastmaster.tame");
    CHECK(move_mul(b.r.fake, 50) == 0.0f);
    // another beaten wolf under the crosshair, the first one aside: a new channel lets the first go
    place(b.r.fake, 50, 6, 10, 3);
    b.beast(51, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    set_move_mul(b.r.fake, 51, 1.0f);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
    CHECK(move_mul(b.r.fake, 51) == 0.0f);
    // the player walks far away: the held beast stays where it is (it is not dragged along), the channel breaks and
    // the beast is let go
    place(b.r.fake, 1, 0, 10, -40);
    b.seconds(4);
    CHECK(teleports_of(b.r.fake, 51) == 0);
    CHECK(b.noted("too far"));
    CHECK(move_mul(b.r.fake, 51) == 1.0f);
}

TEST_CASE("beastmaster: a beast that still runs off during the taming is put back where it was held, and tamed") {
    BeastRig b;
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    b.look(1, 0, 10.5, 0, 0);
    b.reply("beastmaster.tame");
    place(b.r.fake, 50, 0, 10, 12);  // it runs off
    b.seconds(1);
    REQUIRE(teleports_of(b.r.fake, 50) == 1);  // once in that second, not every step
    const auto t = b.r.fake.teleports.back();
    CHECK(std::hypot(t.x - 0.0, t.z - 5.0) <= 0.5);  // where it was when the taming began (0, 10, 5)
    CHECK((t.flags & EMF_TELEPORT_FIND_GROUND) != 0);
    place(b.r.fake, 50, t.x, 10, t.z);  // the game moved it
    b.seconds(4);
    CHECK(b.noted("You tamed a wolf"));
    CHECK(!b.noted("too far"));
    CHECK(teleports_of(b.r.fake, 50) == 1);
}

TEST_CASE("beastmaster: feeding holds the beast too, and lets it go when the feeding ends") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 100, 100);
    set_move_mul(b.r.fake, 50, 1.0f);
    b.look(1, 0, 10.5, 0, 0);
    b.reply("beastmaster.feed");
    CHECK(move_mul(b.r.fake, 50) == 0.0f);
    b.seconds(11);  // no more bait: it loses interest
    CHECK(b.noted("lost interest"));
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
}

TEST_CASE("beastmaster: a feed with an unknown bait name leaves the beast free to move") {
    BeastRig b({{"bait_wolf", "No_Such_Item"}});
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 100, 100);
    set_move_mul(b.r.fake, 50, 1.0f);
    CHECK(b.reply("beastmaster.feed").find("No_Such_Item") != std::string::npos);
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
}

TEST_CASE("beastmaster: a beast in one player's taming cannot be fed or tamed by another, and is let go once") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    set_move_mul(b.r.fake, 50, 1.0f);
    b.look(1, 0, 10.5, 0, 0);
    b.reply("beastmaster.feed");  // player 1 holds it (feeding)
    CHECK(move_mul(b.r.fake, 50) == 0.0f);
    place(b.r.fake, 2, 0, 10, 9);  // player 2 right behind it, looking at it
    b.look(2, 0, 10.5, 9, 180);
    CHECK(b.reply("beastmaster.feed", 2, 0xBB).find("Someone else") != std::string::npos);
    CHECK(b.reply("beastmaster.tame", 2, 0xBB).find("Someone else") != std::string::npos);
    b.seconds(11);  // player 1's feeding ends: the wolf moves again, and nothing sets it back to 0
    CHECK(b.noted("lost interest"));
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
    b.seconds(11);
    CHECK(move_mul(b.r.fake, 50) == 1.0f);
}

// ---- stepping aside: the game keeps a no-build zone around every creature with the enemy AI (add_no_buildzones_enemy),
// so a beast at your side blocks building and placing there ----
namespace {
// Player `p` holds something to build or place (the game's building cursor has an item), or not.
void building(Fake& f, uint32_t p, bool on) {
    component<ecs::ServerCursor>(f, p).currentBuildingItemInstance.itemId.value = on ? 0x1234u : 0u;
}
size_t mana_paid(const Fake& f, uint32_t p, int32_t cost) {
    size_t n = 0;
    for (const auto& [e, a, d] : f.attr_adds) n += e == p && a == EMF_ATTR_MANA && d == -cost;
    return n;
}
size_t notes_with(const Fake& f, const std::string& needle) {
    size_t n = 0;
    for (const auto& x : f.notes) n += x.find(needle) != std::string::npos;
    return n;
}
}  // namespace

TEST_CASE("beastmaster: beasts step aside while their owner builds and come back after, free; a call meanwhile waits") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    b.spawnable(4);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    REQUIRE(b.r.fake.spawns.size() == 1);
    building(b.r.fake, 1, true);
    b.seconds(2);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) != b.r.fake.destroyed.end());
    CHECK(notes_with(b.r.fake, "step aside while you build") == 1);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, aside") != std::string::npos);  // still out: it comes back
    CHECK(b.hud("out").find("Wolf (aside)") != std::string::npos);
    b.seconds(11);
    CHECK(b.r.fake.spawns.size() == 1);  // not while building
    CHECK(b.reply("beastmaster.call Boar").find("while you build") != std::string::npos);
    building(b.r.fake, 1, false);
    b.seconds(1);
    CHECK(b.r.fake.spawns.size() == 1);  // a moment after (the hammer goes back and forth)
    b.seconds(4);
    REQUIRE(b.r.fake.spawns.size() == 2);  // the wolf is back beside its owner
    CHECK(guid_text(b.r.fake.spawns[1].guid) == kWolfTemplate);
    CHECK(std::abs(b.r.fake.spawns[1].x - 2.0) <= 1.0);
    CHECK(mana_paid(b.r.fake, 1, 15) == 1);  // free: only the first call cost mana
    CHECK(b.hud("out").find("Wolf (out)") != std::string::npos);
    // a second time: no second note; with make_room off they stay
    building(b.r.fake, 1, true);
    b.seconds(2);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1235u) != b.r.fake.destroyed.end());
    CHECK(notes_with(b.r.fake, "step aside while you build") == 1);
    b.r.fake.settings["beastmaster.make_room"] = "false";
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 3);
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 3);  // and stay
}

TEST_CASE("beastmaster: /beastmaster aside sends the beasts away until typed again; calling one brings them back") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    b.spawnable(5);
    CHECK(b.reply("beastmaster.aside").find("No beast of yours is out") != std::string::npos);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(b.reply("beastmaster.aside").find("step aside") != std::string::npos);
    b.seconds(2);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) != b.r.fake.destroyed.end());
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 1);  // away until called back
    CHECK(b.reply("beastmaster.aside").find("come back") != std::string::npos);
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 2);
    CHECK(b.reply("beastmaster.aside").find("step aside") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);  // a call ends it
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 4);  // the boar, and the wolf back
    CHECK(mana_paid(b.r.fake, 1, 15) == 2);
    // a guest's own
    CHECK(b.reply("beastmaster aside", 2, 0xBB).find("No beast of yours is out") != std::string::npos);
    const std::string help = b.reply("beastmaster");
    CHECK(help.find("aside") != std::string::npos);
}

TEST_CASE("beastmaster: an aside by the command ends with the last beast gone home, and a newly tamed beast brings the others back") {
    BeastRig b;
    b.reply("beastmaster.give boar");
    b.spawnable(3);
    CHECK(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(b.reply("beastmaster.aside").find("step aside") != std::string::npos);
    b.seconds(2);
    CHECK(b.reply("beastmaster.dismiss").find("Boar goes home") != std::string::npos);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);  // a weakened wild wolf in front
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    b.seconds(5);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, out") != std::string::npos);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 50u) == b.r.fake.destroyed.end());  // stays
    CHECK(b.hud("out").find("Wolf (out)") != std::string::npos);
    // aside with a beast out, then a tame: the tamed one stays and the others come back
    CHECK(b.reply("beastmaster.aside").find("step aside") != std::string::npos);
    b.seconds(2);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 50u) != b.r.fake.destroyed.end());
    b.beast(51, "aed0ecec-8a98-4fd3-a450-ab4c508b0ad4", 0, 5, kWildBeast, 20, 100);  // a weakened wild boar
    CHECK(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    b.seconds(5);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 51u) == b.r.fake.destroyed.end());
    CHECK(b.hud("out").find("(aside)") == std::string::npos);
    CHECK(b.r.fake.spawns.size() == 2);  // the boar's first call, and the wolf back
}

TEST_CASE("beastmaster: while aside by the command the wheel has Come back and the list says aside; picking it brings them back") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(3);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    CHECK(index_of(b.r.fake.wheels[0].items, "Come back") < 0);
    CHECK(b.reply("beastmaster.aside").find("Come back") != std::string::npos);  // says how
    b.seconds(2);
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("Wolf - wolf, aside") != std::string::npos);
    CHECK(list.find("/beastmaster aside") != std::string::npos);
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 2);
    const auto w = b.r.fake.wheels[1];
    const int back = index_of(w.items, "Come back");
    REQUIRE(back >= 0);
    w.cb(kClient, back, w.user);
    b.seconds(1);
    CHECK(b.noted("Your beasts come back"));
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 2);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, out") != std::string::npos);
}

TEST_CASE("beastmaster: beasts also step aside while another player builds within 20 m of them, and come back when that player is gone") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(3);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    building(b.r.fake, 2, true);  // the guest builds 30 m away: too far
    b.seconds(3);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) == b.r.fake.destroyed.end());
    place(b.r.fake, 2, 12, 10, 0);  // now 10 m from the wolf
    b.seconds(2);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) != b.r.fake.destroyed.end());
    CHECK(notes_with(b.r.fake, "a player builds near them") == 1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf").find("is out already") != std::string::npos);
    b.reply("beastmaster.give boar");
    CHECK(b.reply("beastmaster.call Boar").find("while a player builds near you") != std::string::npos);
    place(b.r.fake, 2, 60, 10, 0);  // the builder walks off
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 2);  // the wolf is back
}

TEST_CASE("beastmaster: building is noticed in the step it starts; aside off while still building says when they come") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(3);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    building(b.r.fake, 1, true);
    b.r.fake.tick += 1;  // the next game step (1/60 s later), not the next second
    b.host->tick(kServer);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) != b.r.fake.destroyed.end());
    CHECK(b.reply("beastmaster.aside").find("step aside") != std::string::npos);
    const std::string off = b.reply("beastmaster.aside");
    CHECK(off.find("a few seconds after you stop building") != std::string::npos);
    CHECK(off != "Your beasts come back.");
    b.reply("beastmaster.give boar");
    CHECK(b.reply("beastmaster.call Boar").find("a few seconds after you stop building") != std::string::npos);
}

// ---- species_extra.lua: more beasts from a file beside Beastmaster's own (a creature pack that cannot be shared, a
// modder's own creature), read when the mod starts. The GUIDs here are made up. ----
namespace {
constexpr const char* kTrollGraph = "7e0d1c2b-3a4f-4b5c-8d6e-0f1a2b3c4d05";
constexpr const char* kTrollTemplate = "7e0d1c2b-3a4f-4b5c-8d6e-0f1a2b3c4d02";
constexpr const char* kOgreGraph = "7e0d1c2b-3a4f-4b5c-8d6e-0f1a2b3c4d15";
constexpr const char* kOgreTemplate = "7e0d1c2b-3a4f-4b5c-8d6e-0f1a2b3c4d12";
constexpr int kVukah = 3;

// Writes species_extra.lua beside the copied mod and lets the host reload it (`step`: a later write needs a later one),
// then plays a second so the world's data reaches the reloaded mod (as edit_main).
void extra_species(BeastRig& b, const std::string& lua, int step = 1) {
    write_later(b.folder + L"\\species_extra.lua", lua, step);
    b.host->check_reload_now();
    REQUIRE_MESSAGE(b.host->status(b.id) == "running", b.host->status(b.id));
    b.seconds(1);
}

std::string entry(const char* id, const char* name, const char* tpl, const char* graph, const std::string& more = "") {
    return std::string("\t{ id = \"") + id + "\", name = \"" + name + "\", template = \"" + tpl + "\", graph = \"" +
           graph + "\"" + more + " },\n";
}
}  // namespace

TEST_CASE("beastmaster: a species_extra.lua beside the mod adds its beasts: known by graph and faction, tamed, listed, called with its template and health") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 3") == "unknown");
    extra_species(b, "return {\n" + entry("troll", "Troll", kTrollTemplate, kTrollGraph,
                                          ", plural = \"trolls\", factions = { [3] = true }, health_mul = 3") + "}\n");
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 3") == "troll");
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 6") == "unknown");  // not its faction
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6") == "wolf");       // the mod's own stay
    b.beast(50, kTrollGraph, 0, 5, kVukah, 20, 100);
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the troll") != std::string::npos);
    b.seconds(4);
    CHECK(b.noted("You tamed a troll: Troll"));
    CHECK(b.reply("beastmaster.list").find("1. Troll - troll, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call 1").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kTrollTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 80);  // 40 + 80 = 120 = 3 x 40
    CHECK(b.reply("beastmaster.give troll").find("troll") != std::string::npos);
}

TEST_CASE("beastmaster: a species_extra.lua beast is fed the bait its entry names; one without bait takes none") {
    BeastRig b;
    b.r.fake.item_names[kDogBait] = "Food_T2_Dog_Bait";
    extra_species(b, "return {\n" + entry("troll", "Troll", kTrollTemplate, kTrollGraph, ", bait = \"Food_T2_Dog_Bait\"") +
                         entry("ogre", "Ogre", kOgreTemplate, kOgreGraph) + "}\n");
    b.beast(50, kOgreGraph, 0, 5, kVukah, 100, 100);   // ahead (+z)
    b.beast(51, kTrollGraph, 5, 0, kVukah, 100, 100);  // to the right (+x)
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame") == "The ogre is too strong: bring it under 25 % health.");
    CHECK(b.reply("beastmaster.feed") == "The ogre takes no bait: bring it under 25 % health to tame it.");
    CHECK(b.r.fake.takes.empty());
    b.look(1, 0, 10.5, 0, 90);
    CHECK(b.reply("beastmaster.tame").find("feed it its bait (Food_T2_Dog_Bait)") != std::string::npos);
    CHECK(b.reply("beastmaster.feed") == "You offer the troll some Food_T2_Dog_Bait.");
    REQUIRE(b.r.fake.takes.size() == 1);
    CHECK(b.r.fake.takes.back().item == kDogBait);
}

TEST_CASE("beastmaster: a species_extra.lua beast whose bait the game does not know points at its entry") {
    BeastRig b;
    extra_species(b, "return {\n" + entry("ogre", "Ogre", kOgreTemplate, kOgreGraph, ", bait = \"No_Such_Item\"") + "}\n");
    b.beast(50, kOgreGraph, 0, 5, kVukah, 100, 100);
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.feed") == "The game has no item named No_Such_Item: check its bait in species_extra.lua.");
}

TEST_CASE("beastmaster: species_extra.lua entries without id, name, template or graph, or with a taken id, are skipped with a log line") {
    BeastRig b;
    CHECK(logged(b.r.fake, "species_extra") == 0);  // no file: nothing to say
    extra_species(b, "return {\n" + entry("wolf", "Grey wolf", kTrollTemplate, kTrollGraph) +
                         "\t{ name = \"Nameless\", template = \"" + kOgreTemplate + "\", graph = \"" + kOgreGraph + "\" },\n" +
                         "\t{ id = \"ghost\", name = \"Ghost\", template = \"" + kOgreTemplate + "\" },\n" +
                         "\t\"ogre\",\n" +
                         entry("troll", "Troll", kTrollTemplate, "7E0D1C2B-3A4F-4B5C-8D6E-0F1A2B3C4D05") + "}\n");
    CHECK(logged(b.r.fake, "species_extra.lua: entry 1 (wolf) skipped: the id wolf is taken") == 1);
    CHECK(logged(b.r.fake, "species_extra.lua: entry 2 skipped: it has no id") == 1);
    CHECK(logged(b.r.fake, "species_extra.lua: entry 3 (ghost) skipped: it has no graph") == 1);
    CHECK(logged(b.r.fake, "species_extra.lua: entry 4 skipped: not a table") == 1);
    CHECK(logged(b.r.fake, "species_extra.lua: 1 beast added (troll)") == 1);
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 3") == "troll");  // its graph in any case
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6") == "wolf");
    CHECK(b.reply("beastmaster.give ghost").find("no species ghost") != std::string::npos);
}

TEST_CASE("beastmaster: a species_extra.lua that does not load or gives no list is logged; the mod runs on with its own beasts") {
    BeastRig b;
    extra_species(b, "return {\n\t{ id = \"troll\",\n");  // cut short
    CHECK(logged(b.r.fake, "species_extra.lua did not load:") == 1);
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6") == "wolf");
    extra_species(b, "return 5\n", 2);
    CHECK(logged(b.r.fake, "species_extra.lua gave no list of beasts") == 1);
    CHECK(b.reply("beastmaster.give wolf").find("added") != std::string::npos);
}

TEST_CASE("wild beasts: a species_extra.lua beast with a pack comes as a wild beast too") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    for (const char* t : {kBearTemplate, kAurochsTemplate, kSnakeTemplate, kMinotaurTemplate})
        b.r.fake.missing_templates.insert(guid_bytes(t));
    no_rare_snakes(b.r.fake);
    extra_species(b, "return {\n" + entry("troll", "Troll", kTrollTemplate, kTrollGraph, ", pack = \"a private pack\"") + "}\n");
    b.spawnable(2);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns.back().guid) == kTrollTemplate);
}
