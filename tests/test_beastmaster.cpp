// Beastmaster (mods/beastmaster), run by the Lua host from a copy of the shipped folder, with fake engine events,
// components and commands (tests/lua_mod_rig.h).
#include "lua_mod_rig.h"

#include <regex>

#include "loader/portraits_plan.h"

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
        with_windows(r.api);     // a v24 core
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
constexpr uint32_t kLeanMeat = 0x4d0a0002;  // the wolves' bait
}  // namespace

TEST_CASE("beastmaster: the roster names beasts uniquely, holds 8 per player, and comes back from the world's data") {
    BeastRig b;
    for (int i = 1; i <= 8; ++i) CHECK(b.reply("beastmaster.give wolf").find("added") != std::string::npos);
    const std::string ninth = b.reply("beastmaster.give wolf");
    CHECK(ninth.find("8") != std::string::npos);
    CHECK(ninth.find("added") == std::string::npos);
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("1. Wolf - wolf, loyalty 1, at home") != std::string::npos);
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
    REQUIRE(b.reply("beastmaster.list").find("1. Wolf - wolf, loyalty 1, out") != std::string::npos);
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
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, loyalty 1, out") != std::string::npos);
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

TEST_CASE("beastmaster: each beast's bait suits it and when you meet it: lean meat from level 1, the game's animal baits later") {
    BeastRig b;
    const std::map<std::string, std::string> want = {
        {"wolf", "Food_T1_raw_bad_meat_LeanMeat"},  // wolves are there from level 1: goats, hares and rats drop it
        {"boar", "Food_T1_raw_bad_meat_LeanMeat"},
        {"snake", "Food_T1_raw_bad_meat_LeanMeat"},
        {"hound", "Food_T2_Dog_Bait"},
        {"sabretooth", "Food_T2_Cat_Bait"},
        {"bear", "Food_T1_raw_fruit_Red_Berries"},
        {"aurochs", "Food_T1_Goat_Bait"},
        {"crimson_snake", "Food_T2_raw_bad_meat_Game"},
        {"golden_snake", "Food_T3_Turkey_Bait"},
        {"albino_snake", "Food_T5_Yak_Bait"},  // the rarest takes the hardest bait
    };
    for (const auto& [kind, item] : want) CHECK_MESSAGE(b.r.fake.settings["beastmaster.bait_" + kind] == item, kind);
}

TEST_CASE("beastmaster: a healthy beast is not tamed by defeat; with 3 baits it is tamed by feeding") {
    BeastRig b;
    b.r.fake.item_names[kLeanMeat] = "Food_T1_raw_bad_meat_LeanMeat";
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 100, 100);
    CHECK(b.reply("beastmaster.tame").find("too strong") != std::string::npos);
    for (int i = 0; i < 3; ++i) {
        b.reply("beastmaster.feed");
        REQUIRE(b.r.fake.takes.size() == static_cast<size_t>(i + 1));
        const EmfItemsRequest t = b.r.fake.takes.back();
        CHECK(t.item == kLeanMeat);
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

TEST_CASE("beastmaster: calling a wolf costs 15 % mana, spawns it beside you as your ally; a second wolf can come too") {
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
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, loyalty 1, out") != std::string::npos);
    CHECK(b.reply("beastmaster.call Wolf").find("out already") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf 2").find("comes") != std::string::npos);  // two slots: two wolves
    CHECK(b.r.fake.spawns.size() == 2);
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

TEST_CASE("beastmaster: a beast that dies rests 60 s, its body is removed, and it can be called again after") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.spawnable(2);
    b.reply("beastmaster.call Wolf");
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 0;
    b.seconds(2);
    CHECK(contains(b.r.fake.destroyed, 1234));  // no loot from our own beasts
    CHECK(b.reply("beastmaster.list").find("rests 0:5") != std::string::npos);
    CHECK(b.reply("beastmaster.call Wolf").find("rests") != std::string::npos);
    b.seconds(60);
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
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf, loyalty 1, at home") != std::string::npos);
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

TEST_CASE("beastmaster: a beast 70 m away for 3 s, or 130 m away at once, is teleported beside its owner") {
    BeastRig b;
    wolf_out(b);
    place(b.r.fake, 1234, 50, 10, 0);  // within 60 m: never
    b.seconds(4);
    CHECK(teleports_of(b.r.fake, 1234) == 0);
    place(b.r.fake, 1234, 70, 10, 0);
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
    place(b.r.fake, 1234, 130, 10, 0);  // far at once
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

// ---- modes: passive, defending (the first), aggressive; one for all of a player's beasts ----
namespace {
// A creature at (x, 10, z): faction, alive (100 of 100).
void creature(BeastRig& b, uint32_t id, double x, double z, int faction) {
    place(b.r.fake, id, x, 10, z);
    component<ecs::Faction>(b.r.fake, id).faction = static_cast<emf::gen::FactionType>(faction);
    b.r.fake.attrs[{id, EMF_ATTR_HEALTH}] = 100;
    b.r.fake.attrs[{id, EMF_ATTR_MAX_HEALTH}] = 100;
    add_entity(b.r.fake, id);
}
constexpr int kVukahFoe = 3, kSkeletonFoe = 10, kWildlifeAnimal = 5, kFarmAnimal = 12;
}  // namespace

TEST_CASE("beastmaster: mode is defending at first; /beastmaster mode sets passive, defending or aggressive, kept with the world") {
    BeastRig b;
    CHECK(b.reply("beastmaster.mode").find("defending") != std::string::npos);
    CHECK(b.reply("beastmaster.mode aggressive").find("aggressive") != std::string::npos);
    CHECK(b.reply("beastmaster.mode").find("aggressive") != std::string::npos);
    CHECK(b.reply("beastmaster.mode sleepy").find("passive, defending or aggressive") != std::string::npos);
    CHECK(b.reply("beastmaster.mode").find("aggressive") != std::string::npos);  // unchanged
    CHECK(b.reply("beastmaster.mode Passive").find("passive") != std::string::npos);
    CHECK(b.reply("beastmaster.mode", 2, 0xBB).find("defending") != std::string::npos);  // each player their own
    b.seconds(2);
    CHECK(b.saved().find("m 0x00000000000000aa passive") != std::string::npos);
    // the same world loads again: the mode comes back with the beasts
    write(b.data, "m 0x00000000000000aa aggressive\nb 0x00000000000000aa wolf 0 0 Fang\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    CHECK(b.reply("beastmaster.mode").find("aggressive") != std::string::npos);
    CHECK(b.reply("beastmaster.list").find("Fang") != std::string::npos);
}

TEST_CASE("beastmaster: the wheel's Mode item steps defending, aggressive, passive; the line at the bottom left names it") {
    BeastRig b;
    wolf_out(b);
    b.seconds(2);
    CHECK(b.hud("out").find("Beasts (defending): Wolf (out)") != std::string::npos);
    for (const char* next : {"aggressive", "passive", "defending"}) {
        CAPTURE(next);
        b.r.fake.wheels.clear();
        press(b, VK_F3);
        REQUIRE(b.r.fake.wheels.size() == 1);
        const auto w = b.r.fake.wheels[0];
        const int mode = index_of(w.items, "Mode: ");
        REQUIRE(mode >= 0);
        w.cb(kClient, mode, w.user);
        b.seconds(2);
        CHECK(b.noted(std::string("Your beasts are ") + next));
        CHECK(b.hud("out").find(std::string("Beasts (") + next + "): Wolf (out)") != std::string::npos);
    }
}

TEST_CASE("beastmaster: aggressive beasts go at the nearest hostile creature within 25 m of their owner") {
    BeastRig b;
    wolf_out(b);
    creature(b, 70, 0, 20, kSkeletonFoe);    // 20 m from the owner at (0, 10, 0)
    creature(b, 71, 0, 12, kVukahFoe);       // 12 m: the nearest foe
    creature(b, 72, 3, 3, kFarmAnimal);      // a farm animal: never
    creature(b, 73, -3, 3, kWildlifeAnimal); // wildlife (a goat, a hare): never
    creature(b, 74, 4, 0, 11);               // another ally: never
    creature(b, 75, 0, 40, kSkeletonFoe);    // beyond 25 m: not yet
    b.seconds(2);
    CHECK(attacks(b).empty());               // defending: they go only at what attacks you or them
    b.reply("beastmaster.mode aggressive");
    b.seconds(2);
    auto a = attacks(b);
    REQUIRE(!a.empty());
    CHECK(a.back() == std::make_pair(1234u, 71u));
    for (const auto& [attacker, target] : a) CHECK((target == 70u || target == 71u));
    b.r.fake.attrs[{71, EMF_ATTR_HEALTH}] = 0;  // dead: the next nearest
    b.seconds(3);
    CHECK(attacks(b).back() == std::make_pair(1234u, 70u));
    b.reply("beastmaster.mode passive");
    const size_t before = attacks(b).size();
    b.seconds(3);
    CHECK(attacks(b).size() == before);      // passive: none
}

TEST_CASE("beastmaster: aggressive beasts leave bosses and players alone") {
    BeastRig b;
    wolf_out(b);
    creature(b, 80, 0, 10, kSkeletonFoe);
    component<ecs::BossHealthBar>(b.r.fake, 80);  // a boss
    b.reply("beastmaster.mode aggressive");
    b.seconds(3);
    CHECK(attacks(b).empty());
}

namespace {
// What the game's AI of `id` fights: an alarmed aggro target and a chosen target, both `foe`.
void fighting(BeastRig& b, uint32_t id, uint32_t foe) {
    auto& a = component<ecs::DynamicAggro>(b.r.fake, id);
    a.targets[0].state = ecs::AggroTargetState::Alarmed;
    a.targets[0].aggro = 50.0f;
    a.targets[0].entityId.id = foe;
    component<ecs::SelectedTargets>(b.r.fake, id).targets[0].targetEntityId.id = foe;
}
// A component as it is now (component() makes a fresh one).
template <class T>
const T& now_of(BeastRig& b, uint32_t id) {
    return *reinterpret_cast<const T*>(b.r.fake.components.at({T::kHash, id}).data());
}
bool still_fighting(BeastRig& b, uint32_t id) {
    const auto& a = now_of<ecs::DynamicAggro>(b, id);
    return a.targets[0].state != ecs::AggroTargetState::Unknown || a.targets[0].aggro != 0.0f ||
           now_of<ecs::SelectedTargets>(b, id).targets[0].targetEntityId.id != 0;
}
}  // namespace

TEST_CASE("beastmaster: passive beasts' aggro and chosen targets are cleared every step; defending ones are left to fight") {
    BeastRig b;
    wolf_out(b);
    creature(b, 70, 0, 10, kSkeletonFoe);
    fighting(b, 1234, 70);
    b.seconds(2);
    CHECK(still_fighting(b, 1234));  // defending: it fights
    b.reply("beastmaster.mode passive");
    b.seconds(1);
    CHECK_FALSE(still_fighting(b, 1234));
    fighting(b, 1234, 70);           // the game picks it again: cleared again
    b.seconds(1);
    CHECK_FALSE(still_fighting(b, 1234));
}

TEST_CASE("beastmaster: passive beasts still go where Attack sends them, for 20 s") {
    BeastRig b;
    wolf_out(b);
    b.reply("beastmaster.mode passive");
    creature(b, 70, 0, 10, kSkeletonFoe);
    b.look(1, 0, 10.5, 0, 0);        // looking at it
    REQUIRE(b.reply("beastmaster.attack").find("attack") != std::string::npos);
    fighting(b, 1234, 70);
    b.seconds(5);
    CHECK(still_fighting(b, 1234));  // sent: left to fight
    b.seconds(16);
    CHECK_FALSE(still_fighting(b, 1234));  // 20 s on: passive again
}

TEST_CASE("beastmaster: a beast more than 45 m from its owner drops its chase and is told to guard them again, in any mode") {
    BeastRig b;
    wolf_out(b);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234).guardEntityId.id = 1;  // it guards its owner
    creature(b, 70, 0, 60, kSkeletonFoe);
    place(b.r.fake, 1234, 0, 10, 40);  // chasing, 40 m away: still within
    fighting(b, 1234, 70);
    b.seconds(2);
    CHECK(still_fighting(b, 1234));
    const size_t guards_before = b.guards().size();
    place(b.r.fake, 1234, 0, 10, 50);  // 50 m: beyond 45
    b.seconds(1);
    CHECK_FALSE(still_fighting(b, 1234));
    CHECK(b.guards().size() > guards_before);  // told to come back to its owner
    CHECK(teleports_of(b.r.fake, 1234) == 0);  // it runs back: within 60 m it is not teleported
}

// ---- loyalty: grows with the kills a beast takes part in; kept with the world ----
TEST_CASE("loyalty: a beast's loyalty and name are kept with the world; an older save loads at loyalty 1") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    CHECK(b.reply("beastmaster.loyalty Wolf 3").find("3") != std::string::npos);
    CHECK(b.reply("beastmaster.rename Wolf Grey Fang").find("Grey Fang") != std::string::npos);
    b.seconds(2);
    CHECK(b.saved().find("y 0x00000000000000aa 30 Grey Fang") != std::string::npos);  // 10 + 20 points
    write(b.data, "b 0x00000000000000aa wolf 0 0 Grey Fang\ny 0x00000000000000aa 30 Grey Fang\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    CHECK(b.reply("beastmaster.list").find("1. Grey Fang - wolf, loyalty 3, at home") != std::string::npos);
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\n");  // a save from before loyalty
    b.r.fake.session = 3;
    fire_load(1);
    b.seconds(1);
    CHECK(b.reply("beastmaster.list").find("1. Fang - wolf, loyalty 1, at home") != std::string::npos);
}

TEST_CASE("loyalty: the admin loyalty command is for the host's console only") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    CHECK(b.reply("beastmaster.loyalty Wolf 11").find("1-10") != std::string::npos);
    CHECK(b.reply("beastmaster.loyalty Wolf 5", 1, 0xAA).find("console") != std::string::npos);  // from the chat
    CHECK(b.reply("beastmaster.list").find("loyalty 1,") != std::string::npos);
}

TEST_CASE("loyalty: rename takes 1-20 characters and no name the collection has") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");  // Wolf, Wolf 2
    CHECK(b.reply("beastmaster.rename 1 Rex").find("Rex") != std::string::npos);
    CHECK(b.reply("beastmaster.list").find("1. Rex - wolf") != std::string::npos);
    CHECK(b.reply("beastmaster.rename Rex wolf 2").find("taken") != std::string::npos);
    CHECK(b.reply("beastmaster.rename 2").find("1-20") != std::string::npos);
    CHECK(b.reply("beastmaster.rename 2 " + std::string(21, 'a')).find("1-20") != std::string::npos);
    CHECK(b.reply("beastmaster.rename 9 Max").find("No such beast") != std::string::npos);
    CHECK(b.reply("beastmaster.list").find("2. Wolf 2 - wolf") != std::string::npos);
}

namespace {
// A component as it is now, to change (component() makes a fresh one).
template <class T>
T& live_of(BeastRig& b, uint32_t id) {
    return *reinterpret_cast<T*>(b.r.fake.components.at({T::kHash, id}).data());
}
constexpr int64_t kSpawnNs = 7000000000000LL;  // the game's time (ns) at a called beast's spawn: its SpawnTime
// The creature the next call spawns (id): its template's damage multiplier 1, its base health 100, its SpawnTime
// kSpawnNs, optionally a scale.
void next_beast(BeastRig& b, uint32_t id, float scale = 0.0f) {
    b.spawnable(1);
    component<ecs::DamageMod>(b.r.fake, id).dataStorage[2] = 1.0f;
    component<ecs::Health>(b.r.fake, id).dataStorage[2] = 100;
    component<ecs::SpawnTime>(b.r.fake, id).spawnTime.value = kSpawnNs;
    if (scale > 0.0f) component<ecs::ScaleComponent>(b.r.fake, id).uniformScale = scale;
}
}  // namespace

TEST_CASE("loyalty: a called wolf has its role, 1.2x health and 1.15x damage; at loyalty 10 1.44x and 1.38x") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(now_of<ecs::Health>(b, 1234).dataStorage[3] == 20);  // the adder: 100 x 0.2
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15));
    b.reply("beastmaster.dismiss all");
    b.reply("beastmaster.loyalty Wolf 10");
    next_beast(b, 1235);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(now_of<ecs::Health>(b, 1235).dataStorage[3] == 44);  // 100 x 1.2 x 1.2 - 100
    CHECK(now_of<ecs::DamageMod>(b, 1235).dataStorage[2] == doctest::Approx(1.38));
}

TEST_CASE("loyalty: a tamed bear's health is its role (2.2x), not the wild bear's 2.5x") {
    BeastRig b;
    b.reply("beastmaster.give bear");
    next_beast(b, 1234, 1.4f);
    REQUIRE(b.reply("beastmaster.call Bear").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(now_of<ecs::Health>(b, 1234).dataStorage[3] == 120);
    CHECK(now_of<ecs::ScaleComponent>(b, 1234).uniformScale == doctest::Approx(1.4));  // loyalty 1: its own size
}

namespace {
// The size a spawn was given (spawn_ex's ScaleComponent), or -1 when none.
float scale_given(const Fake::Spawn& sp) {
    for (const auto& [hash, data] : sp.extra)
        if (hash == ecs::ScaleComponent::kHash && data.size() >= sizeof(ecs::ScaleComponent))
            return reinterpret_cast<const ecs::ScaleComponent*>(data.data())->uniformScale;
    return -1.0f;
}
}  // namespace

TEST_CASE("loyalty: a beast is called at its loyalty's size (a bear 1.4 x 1.25 at 10, a game wolf 1.25); at loyalty 1 its own") {
    // the game draws a creature at the size it is made with: a ScaleComponent changed later is not drawn
    BeastRig b;
    b.reply("beastmaster.give bear");
    b.reply("beastmaster.loyalty Bear 10");
    next_beast(b, 1234, 1.4f);
    REQUIRE(b.reply("beastmaster.call Bear").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(scale_given(b.r.fake.spawns[0]) == doctest::Approx(1.75));
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.loyalty Wolf 10");
    next_beast(b, 1235);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 2);
    CHECK(scale_given(b.r.fake.spawns[1]) == doctest::Approx(1.25));  // the game's wolf grows too
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.dismiss all");  // two out: the most
    next_beast(b, 1236);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 3);
    CHECK(scale_given(b.r.fake.spawns[2]) == -1.0f);  // loyalty 1: as its template makes it
}

TEST_CASE("loyalty: a beast out follows its owner's level, also when the owner levels up") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    CHECK(now_of<ecs::Level>(b, 1234).dataStorage[0] == 10);  // its owner's
    component<ecs::DynamicEnemyScaling>(b.r.fake, 1234).nextUpdateTime.value = 999;
    live_of<ecs::Level>(b, 1).dataStorage[0] = 11;  // the owner levels up
    b.seconds(2);
    CHECK(now_of<ecs::Level>(b, 1234).dataStorage[0] == 11);
    CHECK(now_of<ecs::DynamicEnemyScaling>(b, 1234).nextUpdateTime.value == 0);  // the game rescales it
}

namespace {
bool healed(BeastRig& b, uint32_t id) {
    for (const auto& [e, a, d] : b.r.fake.attr_adds)
        if (e == id && a == EMF_ATTR_HEALTH && d > 0) return true;
    return false;
}
}  // namespace

TEST_CASE("loyalty: a beast heals 5 % of its maximum health a second, 5 s after its last hit") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(6);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 40;  // 40 of 100, hurt in a fight
    deliver(b.r.fake, "HitEvent", hit_event(60, 1234));
    b.r.fake.attr_adds.clear();
    b.seconds(3);
    CHECK_FALSE(healed(b, 1234));                 // 3 s after its last hit: not yet
    b.seconds(3);
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 5));     // 5 % of 100
    b.r.fake.attr_adds.clear();
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 100;
    b.seconds(2);
    CHECK_FALSE(healed(b, 1234));                 // full: nothing more
}

TEST_CASE("loyalty: a hostile foe the beast hit in the last 30 s dies: a point; wildlife, farm animals and old hits give none") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    next_beast(b, 1235);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    creature(b, 70, 0, 10, kSkeletonFoe);
    deliver(b.r.fake, "HitEvent", hit_event(1234, 70));
    deliver(b.r.fake, "HitEvent", hit_event(1235, 70));
    deliver(b.r.fake, "EntityDiedEvent", died_event(70));
    b.seconds(1);
    CHECK(b.saved().find("y 0x00000000000000aa 1 Wolf") != std::string::npos);  // both took part: a point each
    CHECK(b.saved().find("y 0x00000000000000aa 1 Boar") != std::string::npos);
    creature(b, 72, 3, 3, kFarmAnimal);  // a farm animal: no point
    deliver(b.r.fake, "HitEvent", hit_event(1234, 72));
    deliver(b.r.fake, "EntityDiedEvent", died_event(72));
    creature(b, 71, 0, 12, kSkeletonFoe);  // hit 31 s before it dies: no point
    deliver(b.r.fake, "HitEvent", hit_event(1234, 71));
    b.seconds(31);
    deliver(b.r.fake, "EntityDiedEvent", died_event(71));
    creature(b, 73, 0, 14, kSkeletonFoe);  // it only hit the wolf: no point
    deliver(b.r.fake, "HitEvent", hit_event(73, 1234));
    deliver(b.r.fake, "EntityDiedEvent", died_event(73));
    b.seconds(1);
    CHECK(b.saved().find("y 0x00000000000000aa 1 Wolf") != std::string::npos);
}

TEST_CASE("loyalty: the tenth point brings loyalty 2: the owner hears it, the beast is stronger at once and stays so") {
    BeastRig b({{"rest_s", "5"}});
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    for (uint32_t i = 0; i < 10; ++i) {
        if (i == 9) CHECK(b.reply("beastmaster.list").find("loyalty 1,") != std::string::npos);
        creature(b, 100 + i, 0, 10, kSkeletonFoe);
        deliver(b.r.fake, "HitEvent", hit_event(1234, 100 + i));
        deliver(b.r.fake, "EntityDiedEvent", died_event(100 + i));
        b.seconds(1);
    }
    CHECK(b.noted("Wolf reached loyalty 2: a talent point to spend at a stable."));
    CHECK(b.reply("beastmaster.list").find("loyalty 2,") != std::string::npos);
    const double damage = 1.15 * (1 + 0.2 / 9);
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(damage));
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));  // it falls
    next_beast(b, 1235);
    b.seconds(12);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(b.reply("beastmaster.list").find("loyalty 2,") != std::string::npos);
    CHECK(now_of<ecs::DamageMod>(b, 1235).dataStorage[2] == doctest::Approx(damage));
}

TEST_CASE("loyalty: a beast that falls rests 60 s, so it is back for the next fight but not in this one") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    CHECK(b.reply("beastmaster.list").find("rests 1:00") != std::string::npos);
    CHECK(b.r.fake.settings["beastmaster.loyalty_pace"] == "1");
}

TEST_CASE("loyalty: a name that is a number or 'all' is refused (beasts are called and sent home by number, and all)") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    for (const char* name : {"2", "007", "1e0", "all", "ALL"}) {
        CAPTURE(name);
        CHECK(b.reply(std::string("beastmaster.rename Wolf ") + name).find("number") != std::string::npos);
    }
    CHECK(b.reply("beastmaster.list").find("1. Wolf - wolf") != std::string::npos);
}

TEST_CASE("loyalty: a y line belongs to the beast of that name, also a name that looks like a number (an older save)") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa wolf 0 0 2\nb 0x00000000000000aa boar 0 0 Fang\ny 0x00000000000000aa 30 2\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    const std::string list = b.reply("beastmaster.list");
    CHECK(list.find("1. 2 - wolf, loyalty 3") != std::string::npos);
    CHECK(list.find("2. Fang - boar, loyalty 1") != std::string::npos);
}

TEST_CASE("loyalty: a beast on an entity id the game gave out before gets its own template's damage, not the old beast's") {
    BeastRig b;
    b.reply("beastmaster.give bear");
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234, 1.4f);
    component<ecs::DamageMod>(b.r.fake, 1234).dataStorage[2] = 1.5f;  // the pack bear's template
    REQUIRE(b.reply("beastmaster.call Bear").find("comes") != std::string::npos);
    b.seconds(1);
    component<ecs::Faction>(b.r.fake, 1234).faction = static_cast<emf::gen::FactionType>(6);  // the id names another creature now
    b.seconds(2);
    b.r.fake.next_spawn_id = 1234;  // the game gives the id out again: to the wolf
    next_beast(b, 1234);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15));
}

TEST_CASE("loyalty: a beast the game will neither bring back nor call again leaves the mod running") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.r.fake.teleport_ok = false;
    b.r.fake.spawn_fails = true;
    place(b.r.fake, 1234, 130, 10, 0);  // far: it is brought back, and both ways fail
    b.seconds(3);
    CHECK(b.reply("beastmaster.mode").find("defending") != std::string::npos);  // the mod still answers
}

TEST_CASE("loyalty: when the game sets a beast's damage multiplier again (a rescale), its role and loyalty go on top") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    REQUIRE(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15));
    live_of<ecs::DamageMod>(b, 1234).dataStorage[2] = 2.0f;  // the game's rescale (a world's enemy damage factor)
    b.seconds(2);
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(2.3));
    b.seconds(2);
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(2.3));  // not again on top of itself
}

// ---- the stable: active slots, the Beasts window ----
TEST_CASE("stable: active flags and the stabled flag are kept with the world; an older save loads with none active, not stabled") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\nb 0x00000000000000aa boar 0 0 Tusk\n"
                  "a 0x00000000000000aa Tusk\ns 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    b.reply("beastmaster.mode passive");  // a change: the world's data is written
    b.seconds(1);
    CHECK(b.saved().find("a 0x00000000000000aa Tusk") != std::string::npos);
    CHECK(b.saved().find("a 0x00000000000000aa Fang") == std::string::npos);
    CHECK(b.saved().find("s 1") != std::string::npos);
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\n");  // a 1.5.0 save
    b.r.fake.session = 3;
    fire_load(1);
    b.seconds(1);
    b.reply("beastmaster.mode aggressive");
    b.seconds(1);
    CHECK(b.saved().find("\na ") == std::string::npos);
    CHECK(b.saved().find("s 1") == std::string::npos);
}

TEST_CASE("stable: one active slot at the start, more at levels 10, 20 and 30; a level of 0 turns a slot off") {
    BeastRig b;
    component<ecs::Level>(b.r.fake, 1).dataStorage[0] = 5;
    CHECK(b.reply("beastmaster.slots").find("Active slots: 1 (slot 2 at level 10)") != std::string::npos);
    component<ecs::Level>(b.r.fake, 1).dataStorage[0] = 25;
    CHECK(b.reply("beastmaster.slots").find("Active slots: 3 (slot 4 at level 30)") != std::string::npos);
    component<ecs::Level>(b.r.fake, 1).dataStorage[0] = 30;
    CHECK(b.reply("beastmaster.slots").find("Active slots: 4") != std::string::npos);
    BeastRig off({{"slot3_level", "0"}});
    component<ecs::Level>(off.r.fake, 1).dataStorage[0] = 25;
    CHECK(off.reply("beastmaster.slots").find("Active slots: 2 (slot 4 at level 30)") != std::string::npos);
}


// The render model of the game's animal bed (animalprops_farm_animal_sleep_01, the item
// Animal_Farm_Housing_Sleeping_Bed): a stable (mods/beastmaster/stable.lua MODELS).
constexpr const char* kStableModel = "daeb81bb-8398-4128-9107-c75b2e3e7d5b";
// The large animal bed (animalprops_farm_animal_sleep_large_01, Animal_Farm_Housing_Sleeping_Bed_YAK_Sanddigger).
constexpr const char* kLargeStableModel = "7dd9139a-fba7-4e60-8b5a-fa853c250ef7";
// A flame altar (the heart of a base) at (x, 10, z).
void flame_altar(BeastRig& b, uint32_t id, double x, double z) {
    place(b.r.fake, id, x, 10, z);
    component<ecs::BaseIdComponent>(b.r.fake, id);
    add_entity(b.r.fake, id);
}
// A placed prop with a render model (the stable's by default; none: nullptr) at (x, y, z).
void stable_prop(BeastRig& b, uint32_t id, double x, double y, double z, const char* model = kStableModel) {
    place(b.r.fake, id, x, y, z);
    if (model) b.r.fake.templates[{ecs::ModelResource::kHash, id}] = guid_bytes(model);
    component<ecs::StaticCollider>(b.r.fake, id);
    add_entity(b.r.fake, id);
}

TEST_CASE("stable: a built prop with the stable's model in a base is a stable; another prop, or one outside every base, is not") {
    BeastRig b;
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 5, 10, 0);
    stable_prop(b, 951, 8, 10, 0, "2ff37fdb-4bf7-4074-a8c9-b75f47511d24");  // another prop in the base: a dog bed
    stable_prop(b, 952, 300, 10, 0);                                         // a village's, far from every base
    CHECK(b.reply("beastmaster.stable list") == "No stable in this world yet: build an animal bed (the straw bed for farm "
                                                     "animals) in your base, or mark a prop with beastmaster.stable mark.");
    b.seconds(32);  // the next search of the world (one every 30 s)
    const std::string list = b.reply("beastmaster.stable list");
    CHECK(list.find("1. Animal bed at 5, 10, 0 (5 m from you)") != std::string::npos);
    CHECK(list.find("300") == std::string::npos);
    CHECK(list.find("2. ") == std::string::npos);
    stable_prop(b, 953, -6, 10, 0, kLargeStableModel);
    b.seconds(32);
    CHECK(b.reply("beastmaster.stable list").find("2. Large animal bed at -6, 10, 0") != std::string::npos);
}

TEST_CASE("stable: on the host, a stable whose model only the client world has is found") {
    BeastRig b;
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 10, 10, 0);
    // render models are client-side: the model is there while the host's client world steps, not on the server's
    const auto model = b.r.fake.templates[{ecs::ModelResource::kHash, 950}];
    for (int i = 0; i < 4; ++i) {
        b.r.fake.templates[{ecs::ModelResource::kHash, 950}] = model;
        b.host->tick(kClient);
        b.r.fake.templates.erase({ecs::ModelResource::kHash, 950});
        b.seconds(1);
    }
    CHECK(b.reply("beastmaster.stable list").find("1. Animal bed at 10, 10, 0") != std::string::npos);
}

TEST_CASE("stable: on the host, a bed built while the world runs counts within 10 s; one taken down stops counting as fast") {
    BeastRig b;
    flame_altar(b, 900, 0, 0);
    // render models are client-side: the model is on the client world only, which steps with the server's
    const auto model = guid_bytes(kStableModel);
    auto step_both = [&](int secs) {
        for (int i = 0; i < secs * 60; ++i) {
            b.r.fake.templates[{ecs::ModelResource::kHash, 950}] = model;
            b.host->tick(kClient);
            b.r.fake.templates.erase({ecs::ModelResource::kHash, 950});
            b.r.fake.tick += 1;
            b.host->tick(kServer);
        }
    };
    step_both(35);  // the first searches of both worlds are done
    stable_prop(b, 950, 10, 10, 0, nullptr);
    step_both(10);
    CHECK(b.reply("beastmaster.stable list").find("1. Animal bed at 10, 10, 0") != std::string::npos);
    remove_entity(b.r.fake, 950);  // taken down
    step_both(10);
    CHECK(b.reply("beastmaster.stable list").rfind("No stable in this world now (there was one: only active beasts can be "
                                                      "called): build an animal bed", 0) == 0);
}

TEST_CASE("stable: the host marks a prop as a stable, it is kept with the world and found again by place after a reload") {
    BeastRig b;
    stable_prop(b, 960, 0, 10, 5, nullptr);  // any prop (a post, a trough): its model unknown here
    b.look(1, 0, 11.5, 0, 0);                // looking along +z at it
    CHECK(b.reply("beastmaster.stable mark") == "Marked: this prop is a stable now (at 0, 10, 5).");
    b.seconds(1);
    CHECK(b.saved().find("k 0.00 10.00 5.00 -") != std::string::npos);
    CHECK(b.reply("beastmaster.stable list").find("1. marked prop at 0, 10, 5 (5 m from you)") != std::string::npos);
    b.r.fake.session = 2;  // a reload: the mark comes back with the world's data, by its place
    fire_load(1);
    b.seconds(1);
    CHECK(b.reply("beastmaster.stable list").find("1. marked prop at 0, 10, 5") != std::string::npos);
    CHECK(b.reply("beastmaster.stable mark") == "That is a stable already.");
    b.look(1, 0, 11.5, 0, 180);  // looking away
    CHECK(b.reply("beastmaster.stable mark").rfind("Look at a prop", 0) == 0);
    for (const auto& c : b.r.fake.commands)  // the host's and admins' command, not every player's
        if (c.name == "beastmaster.stable") CHECK(c.flags == 0u);
}

TEST_CASE("stable: unmark takes the mark off; without camera data mark takes the prop nearest the sender within 3 m") {
    BeastRig b;
    stable_prop(b, 960, 0, 10, 2, nullptr);  // 2 m from the host, whose camera the server does not know
    CHECK(b.reply("beastmaster.stable mark") == "Marked: the prop nearest to you (at 0, 10, 2) is a stable now.");
    CHECK(b.reply("beastmaster.stable unmark") == "Unmarked: the prop at 0, 10, 2 is no stable now.");
    CHECK(b.reply("beastmaster.stable list").rfind("No stable in this world now (there was one: only active beasts can be "
                                                      "called): build an animal bed", 0) == 0);
    b.seconds(1);
    CHECK(b.saved().find("k 0.00") == std::string::npos);
    CHECK(b.reply("beastmaster.stable unmark") == "No marked stable within 8 m.");
    place(b.r.fake, 960, 0, 10, 6);  // too far for a mark without camera data
    CHECK(b.reply("beastmaster.stable mark").rfind("Look at a prop", 0) == 0);
}

namespace {
constexpr uint32_t kE = 0x45;  // the stable_key default
// A prop 3 m in front of player 1, marked as a stable, with player 1 looking at it.
void at_stable(BeastRig& b) {
    stable_prop(b, 960, 0, 10, 3, nullptr);
    b.look(1, 0, 11.5, 0, 0);
    REQUIRE(b.reply("beastmaster.stable mark").rfind("Marked: ", 0) == 0);
}
// The Beasts window as last drawn: the last update, else the window opened.
const Fake::Win& shown(BeastRig& b) {
    return b.r.fake.window_updates.empty() ? b.r.fake.windows.back() : b.r.fake.window_updates.back();
}
int row_at(const Fake::Win& w, const std::string& key) {
    for (size_t i = 0; i < w.keys.size(); ++i)
        if (w.keys[i] == key) return static_cast<int>(i);
    return -1;
}
std::string text_at(const Fake::Win& w, const std::string& key) {
    const int i = row_at(w, key);
    return i < 0 ? "<none>" : w.texts[static_cast<size_t>(i)];
}
uint32_t flags_at(const Fake::Win& w, const std::string& key) {
    const int i = row_at(w, key);
    return i < 0 ? 0xFFFFFFFFu : w.widgets[static_cast<size_t>(i)].flags;
}
bool shows(const Fake::Win& w, const std::string& part) {
    for (const auto& t : w.texts)
        if (t.find(part) != std::string::npos) return true;
    return false;
}
// A click on the open window's row or button `key` (the core's event, on the server world).
void click(BeastRig& b, const std::string& key, uint32_t kind = EMF_UI_CLICK) {
    REQUIRE(!b.r.fake.windows.empty());
    const Fake::Win w = b.r.fake.windows.back();
    EmfUiEvent e{};
    e.size = sizeof e;
    e.kind = kind;
    e.window = w.id.c_str();
    e.key = key.c_str();
    e.from_key = "";
    e.reason = EMF_UI_BY_PLAYER;
    w.cb(kServer, &e, w.user);
}
// Player 1's beasts from a save: Fang (wolf, active), Tusk (boar), Rex (hound); the world has had a stable.
void three_beasts(BeastRig& b) {
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\nb 0x00000000000000aa boar 0 0 Tusk\n"
                  "b 0x00000000000000aa hound 0 0 Rex\na 0x00000000000000aa Fang\ns 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
}
}  // namespace

TEST_CASE("stable: E at a stable opens the Beasts window with the slots and the stable list") {
    BeastRig b;  // level 10: two slots, slot 3 at level 20, slot 4 at level 30
    three_beasts(b);
    at_stable(b);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 1u);
    const Fake::Win& w = b.r.fake.windows.back();
    CHECK(w.id == "beastmaster.stable");
    CHECK(w.title == "Beasts");
    CHECK(w.close_key == kE);
    CHECK(w.place == EMF_UI_PLACE_CENTRE);
    CHECK(w.kind == EMF_WORLD_SERVER);
    CHECK(shows(w, "Active 1/2"));
    CHECK(text_at(w, "active.1") == "Fang");
    CHECK((flags_at(w, "active.2") & EMF_UI_EMPTY) != 0u);
    CHECK(text_at(w, "active.3") == "Level 20");
    CHECK((flags_at(w, "active.3") & EMF_UI_LOCKED) != 0u);
    CHECK(text_at(w, "active.4") == "Level 30");
    CHECK(shows(w, "Stable 3/8"));
    CHECK(text_at(w, "beast.Fang") == "Fang (wolf, loyalty 1, at home, active)");
    CHECK(text_at(w, "beast.Tusk") == "Tusk (boar, loyalty 1, at home)");
    CHECK(text_at(w, "beast.Rex") == "Rex (hound, loyalty 1, at home)");
    for (const char* button : {"make_active", "store", "release"})  // nothing selected yet
        CHECK((flags_at(w, button) & EMF_UI_DISABLED) != 0u);
}

TEST_CASE("stable: Make active puts the selected beast into a free slot; Store takes it out (sent home if out)") {
    BeastRig b;
    three_beasts(b);
    b.spawnable(1);
    REQUIRE(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);
    b.seconds(1);
    const uint32_t fang = 1234;
    at_stable(b);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 1u);
    click(b, "beast.Tusk");  // selecting a beast opens its tab
    CHECK(text_at(shown(b), "tab.beast") == "Tusk");
    CHECK((flags_at(shown(b), "tab.beast") & EMF_UI_SELECTED) != 0u);
    CHECK((flags_at(shown(b), "make_active") & EMF_UI_DISABLED) == 0u);
    CHECK((flags_at(shown(b), "store") & EMF_UI_DISABLED) != 0u);  // not active
    click(b, "make_active");
    CHECK(shows(shown(b), "Tusk is active."));
    click(b, "tab.stable");
    CHECK((flags_at(shown(b), "beast.Tusk") & EMF_UI_SELECTED) != 0u);  // still selected
    CHECK(text_at(shown(b), "active.2") == "Tusk");
    CHECK(shows(shown(b), "Active 2/2"));
    CHECK(shows(shown(b), "Tusk is active."));
    CHECK(b.saved().find("a 0x00000000000000aa Tusk") != std::string::npos);
    click(b, "beast.Rex");  // both slots taken
    click(b, "make_active");
    CHECK(shows(shown(b), "No free active slot: store a beast first."));
    CHECK(b.saved().find("a 0x00000000000000aa Rex") == std::string::npos);
    click(b, "tab.stable");
    click(b, "active.1");  // a slot selects its beast and opens its tab
    CHECK(text_at(shown(b), "tab.beast") == "Fang");
    click(b, "store");
    CHECK(shows(shown(b), "Fang goes to the stable."));
    click(b, "tab.stable");
    CHECK(text_at(shown(b), "active.1") == "Tusk");
    CHECK((flags_at(shown(b), "active.2") & EMF_UI_EMPTY) != 0u);
    CHECK(b.saved().find("a 0x00000000000000aa Fang") == std::string::npos);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), fang) != b.r.fake.destroyed.end());
    CHECK(text_at(shown(b), "beast.Fang") == "Fang (wolf, loyalty 1, at home)");
}

TEST_CASE("stable: Release needs a second click within 5 s, removes the beast (its body too when out) and frees its slot") {
    BeastRig b;
    three_beasts(b);
    b.spawnable(1);
    REQUIRE(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);
    b.seconds(1);
    at_stable(b);
    press(b, kE);
    click(b, "beast.Fang");
    click(b, "release");
    CHECK(shows(shown(b), "Click Release again within 5 s to let Fang go for good."));
    CHECK(b.saved().find("Fang") != std::string::npos);
    b.seconds(6);  // too late: asks again
    click(b, "release");
    CHECK(shows(shown(b), "Click Release again within 5 s"));
    click(b, "release");
    CHECK(shows(shown(b), "Fang is gone: you let it go."));
    CHECK(b.saved().find("Fang") == std::string::npos);
    CHECK(std::find(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), 1234u) != b.r.fake.destroyed.end());
    CHECK(row_at(shown(b), "beast.Fang") == -1);
    CHECK((flags_at(shown(b), "active.1") & EMF_UI_EMPTY) != 0u);
    CHECK(shows(shown(b), "Stable 2/8"));
    for (const char* button : {"make_active", "store", "release"})  // the selection went with it
        CHECK((flags_at(shown(b), button) & EMF_UI_DISABLED) != 0u);
}

TEST_CASE("stable: the window redraws when a beast falls; once closed it is not drawn again") {
    BeastRig b;
    three_beasts(b);
    b.spawnable(1);
    REQUIRE(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);
    b.seconds(1);
    at_stable(b);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 1u);
    CHECK(text_at(shown(b), "beast.Fang") == "Fang (wolf, loyalty 1, out, active)");
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    b.seconds(2);
    CHECK(text_at(shown(b), "beast.Fang").find("Fang (wolf, loyalty 1, rests ") == 0);
    CHECK(row_at(shown(b), "beast.Tusk") >= 0);
    click(b, "", EMF_UI_CLOSED);
    const size_t drawn = b.r.fake.window_updates.size();
    b.seconds(3);
    CHECK(b.r.fake.window_updates.size() == drawn);
}

TEST_CASE("stable: E away from a stable, or looking away from it, opens nothing") {
    BeastRig b;
    three_beasts(b);
    press(b, kE);  // no stable in the world
    CHECK(b.r.fake.windows.empty());
    stable_prop(b, 960, 0, 10, 3, nullptr);
    b.look(1, 0, 11.5, 0, 0);
    REQUIRE(b.reply("beastmaster.stable mark").rfind("Marked: ", 0) == 0);
    b.look(1, 0, 11.5, 0, 180);  // looking away
    press(b, kE);
    CHECK(b.r.fake.windows.empty());
    place(b.r.fake, 1, 0, 10, -6);  // 9 m away, looking at it
    b.look(1, 0, 11.5, -6, 0);
    press(b, kE);
    CHECK(b.r.fake.windows.empty());
}

namespace {
void set_level(BeastRig& b, uint32_t player, uint32_t level) {
    component<ecs::Level>(b.r.fake, player).dataStorage[0] = level;
}
bool noted(BeastRig& b, const std::string& part) {
    for (const auto& n : b.r.fake.notes)
        if (n.find(part) != std::string::npos) return true;
    return false;
}
// Tames the wild wolf 50 in front of player 1 (beaten down to 20 of 100).
void tame_wolf(BeastRig& b) {
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    REQUIRE(b.reply("beastmaster.tame").find("Taming") != std::string::npos);
    b.seconds(4);
}
}  // namespace

TEST_CASE("stable: two wolves can be out with two slots; one slot allows one beast out") {
    BeastRig b;  // level 10: two slots
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");
    b.spawnable(2);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf 2").find("comes") != std::string::npos);
    CHECK(b.r.fake.spawns.size() == 2);

    BeastRig one;
    set_level(one, 1, 5);  // one slot
    one.reply("beastmaster.give wolf");
    one.reply("beastmaster.give boar");
    one.spawnable(2);
    CHECK(one.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    one.seconds(11);
    CHECK(one.reply("beastmaster.call Boar") == "You have 1 beast out (1 active slot): dismiss one first.");
    CHECK(one.r.fake.spawns.size() == 1);
}

TEST_CASE("stable: before a stable any beast can be called; after it only active ones (a stored one says to make it active at a stable)") {
    BeastRig b;  // level 10: two slots
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\nb 0x00000000000000aa boar 0 0 Tusk\n"
                  "b 0x00000000000000aa hound 0 0 Rex\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    b.spawnable(2);
    CHECK(b.reply("beastmaster.call Rex").find("comes") != std::string::npos);  // no stable yet: any beast
    b.reply("beastmaster.dismiss Rex");
    at_stable(b);  // the world's first stable: Fang and Tusk (list order) take the two slots
    b.seconds(1);
    CHECK(b.saved().find("s 1") != std::string::npos);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Rex") == "Rex is in the stable: make it active at a stable (an animal bed in your base).");
    CHECK(b.reply("beastmaster.call Tusk").find("comes") != std::string::npos);
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    CHECK(index_of(b.r.fake.wheels[0].items, "Call Fang") >= 0);
    CHECK(index_of(b.r.fake.wheels[0].items, "Call Rex") < 0);  // stored: not on the wheel
}

TEST_CASE("stable: a beast tamed with no free slot leaves: home before a stable, to the stable after") {
    BeastRig home;
    set_level(home, 1, 5);  // one slot
    home.reply("beastmaster.give boar");
    home.spawnable(1);
    REQUIRE(home.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    tame_wolf(home);
    CHECK(noted(home, "You tamed a wolf: Wolf. It goes home (1 active slot, 1 beast out)."));
    CHECK(contains(home.r.fake.destroyed, 50));
    CHECK(home.reply("beastmaster.list").find("2. Wolf - wolf, loyalty 1, at home") != std::string::npos);

    BeastRig full;
    set_level(full, 1, 5);
    write(full.data, "b 0x00000000000000aa boar 0 0 Tusk\na 0x00000000000000aa Tusk\ns 1\n");
    full.r.fake.session = 2;
    fire_load(1);
    full.seconds(1);
    tame_wolf(full);
    CHECK(noted(full, "You tamed a wolf: Wolf. It goes to your stable (no free active slot)."));
    CHECK(contains(full.r.fake.destroyed, 50));
    CHECK(full.saved().find("a 0x00000000000000aa Wolf") == std::string::npos);

    BeastRig free;  // level 10: a second slot is free
    write(free.data, "b 0x00000000000000aa boar 0 0 Tusk\na 0x00000000000000aa Tusk\ns 1\n");
    free.r.fake.session = 2;
    fire_load(1);
    free.seconds(1);
    tame_wolf(free);
    CHECK(noted(free, "You tamed a wolf: Wolf"));
    CHECK_FALSE(contains(free.r.fake.destroyed, 50));
    const std::string kept = free.saved();
    CAPTURE(kept);
    CHECK(kept.find("a 0x00000000000000aa Wolf") != std::string::npos);
    CHECK(free.reply("beastmaster.list").find("2. Wolf - wolf, loyalty 1, out") != std::string::npos);
}

TEST_CASE("stable: a 1.5.0 save: beasts out beyond the slots go home, all stay callable; at the first stable the beasts out become active, then list order") {
    BeastRig one;
    set_level(one, 1, 5);  // one slot
    write(one.data, "b 0x00000000000000aa wolf 0 1 Fang\nb 0x00000000000000aa boar 0 1 Tusk\n"
                    "b 0x00000000000000aa hound 0 0 Rex\n");
    one.spawnable(2);
    one.r.fake.session = 2;
    fire_load(1);
    one.seconds(2);
    CHECK(one.r.fake.spawns.size() == 1);  // Fang comes back; Tusk goes home
    CHECK(one.reply("beastmaster.list").find("2. Tusk - boar, loyalty 1, at home") != std::string::npos);
    one.reply("beastmaster.dismiss Fang");
    CHECK(one.reply("beastmaster.call Rex").find("comes") != std::string::npos);  // nothing lost, all callable
    at_stable(one);
    one.seconds(1);
    CHECK(one.saved().find("a 0x00000000000000aa Rex") != std::string::npos);  // the one out
    CHECK(one.saved().find("a 0x00000000000000aa Fang") == std::string::npos);
    CHECK(one.saved().find("a 0x00000000000000aa Tusk") == std::string::npos);

    BeastRig three;
    set_level(three, 1, 25);  // three slots
    write(three.data, "b 0x00000000000000aa wolf 0 0 Fang\nb 0x00000000000000aa boar 0 1 Tusk\n"
                      "b 0x00000000000000aa hound 0 0 Rex\nb 0x00000000000000aa sabretooth 0 1 Moss\n");
    three.spawnable(2);
    three.r.fake.session = 2;
    fire_load(1);
    three.seconds(2);
    REQUIRE(three.r.fake.spawns.size() == 2);
    at_stable(three);
    three.seconds(1);
    for (const char* name : {"Tusk", "Moss", "Fang"})
        CHECK(three.saved().find(std::string("a 0x00000000000000aa ") + name) != std::string::npos);
    CHECK(three.saved().find("a 0x00000000000000aa Rex") == std::string::npos);
}

namespace {
// Player 2's beasts from a save: Grey (wolf, active), Hog (boar); a built stable 3 m from player 2 (at 30, 10, 0).
void guest_at_stable(BeastRig& b) {
    write(b.data, "b 0x00000000000000bb wolf 0 0 Grey\nb 0x00000000000000bb boar 0 0 Hog\n"
                  "a 0x00000000000000bb Grey\ns 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    flame_altar(b, 900, 30, 0);
    stable_prop(b, 961, 33, 10, 0);
    b.seconds(32);  // the next search of the world finds it
}
}  // namespace

TEST_CASE("stable: a joined player makes a beast active and stores it by chat near a stable; away from one they are told to go to a stable") {
    BeastRig b;
    guest_at_stable(b);
    CHECK(b.reply("beastmaster.active Hog", 2, 0xBB) == "Hog is active.");
    CHECK(b.saved().find("a 0x00000000000000bb Hog") != std::string::npos);
    CHECK(b.reply("beastmaster.list", 2, 0xBB).find("2. Hog - boar, loyalty 1, at home, active") != std::string::npos);
    CHECK(b.reply("beastmaster.active 2", 2, 0xBB) == "Hog is active already.");
    CHECK(b.reply("beastmaster.store 1", 2, 0xBB) == "Grey goes to the stable.");
    CHECK(b.reply("beastmaster store Grey", 2, 0xBB) == "Grey is in the stable already.");  // a word of /beastmaster
    CHECK(b.saved().find("a 0x00000000000000bb Grey") == std::string::npos);
    place(b.r.fake, 2, 60, 10, 0);  // 27 m from it
    CHECK(b.reply("beastmaster.active Grey", 2, 0xBB) == "Go to a stable (within 10 m) to do that.");
    CHECK(b.reply("beastmaster.active Wolfie", 2, 0xBB) == "No such beast (beastmaster.list shows yours).");
}

TEST_CASE("stable: chat release needs the same command again within 10 s") {
    BeastRig b;
    guest_at_stable(b);
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB) == "Type the same command again within 10 s to let Hog go for good.");
    b.seconds(11);  // too late: asks again
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB) == "Type the same command again within 10 s to let Hog go for good.");
    CHECK(b.reply("beastmaster.release Grey", 2, 0xBB).rfind("Type the same command again", 0) == 0);  // another beast
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB).rfind("Type the same command again", 0) == 0);   // asks again
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB) == "Hog is gone: you let it go.");
    CHECK(b.saved().find("Hog") == std::string::npos);
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB) == "No such beast (beastmaster.list shows yours).");
}

TEST_CASE("stable: a change saved more than 2 s of real time after the rig's last tick still lands (the rig's file clock)") {
    // the flake this pins: the world-data file once went by the wall clock, so a slow test machine between two ticks
    // made save_world_data refuse and the file kept the old text
    BeastRig b;
    guest_at_stable(b);
    Sleep(2100);
    CHECK(b.reply("beastmaster.active Hog", 2, 0xBB) == "Hog is active.");
    CHECK(b.saved().find("a 0x00000000000000bb Hog") != std::string::npos);
}

TEST_CASE("stable: a release asked before the world loaded again needs asking again") {
    BeastRig b;
    guest_at_stable(b);
    REQUIRE(b.reply("beastmaster.release Hog", 2, 0xBB).rfind("Type the same command again", 0) == 0);
    b.r.fake.session = 3;  // the world loads again (its data as saved), within the 10 s
    fire_load(1);
    b.seconds(1);
    CHECK(b.reply("beastmaster.release Hog", 2, 0xBB).rfind("Type the same command again", 0) == 0);
    CHECK(b.saved().find("Hog") != std::string::npos);
}

TEST_CASE("stable: a bed out of the host's sight still counts while the server world has it; a guest uses it there") {
    BeastRig b;
    write(b.data, "b 0x00000000000000bb wolf 0 0 Grey\nb 0x00000000000000bb boar 0 0 Hog\n"
                  "a 0x00000000000000bb Grey\ns 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    flame_altar(b, 900, 30, 0);
    stable_prop(b, 961, 33, 10, 0, nullptr);  // the bed; its model is on the client world while the host sees it
    const auto model = guid_bytes(kStableModel);
    bool seen = true;
    auto step_both = [&](int secs) {
        for (int i = 0; i < secs * 60; ++i) {
            if (seen) b.r.fake.templates[{ecs::ModelResource::kHash, 961}] = model;
            b.host->tick(kClient);
            b.r.fake.templates.erase({ecs::ModelResource::kHash, 961});
            b.r.fake.tick += 1;
            b.host->tick(kServer);
        }
    };
    step_both(10);
    REQUIRE(b.reply("beastmaster.stable list").find("1. Animal bed at 33, 10, 0") != std::string::npos);
    place(b.r.fake, 1, 600, 10, 0);  // the host goes far away: the client world no longer shows the bed
    seen = false;
    step_both(40);
    CHECK(b.reply("beastmaster.stable list").find("1. Animal bed at 33, 10, 0") != std::string::npos);
    CHECK(b.reply("beastmaster.active Hog", 2, 0xBB) == "Hog is active.");  // the guest beside it
    remove_entity(b.r.fake, 961);  // taken down while the host is away: gone at the server world's next search
    step_both(35);
    CHECK(b.reply("beastmaster.stable list").rfind("No stable in this world now", 0) == 0);
}

TEST_CASE("stable: before the world has a stable the wheel says what to build; a stored beast's refusal says what a stable is") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    const int hint = index_of(b.r.fake.wheels[0].items, "Stable: animal bed");
    REQUIRE(hint >= 0);
    b.r.fake.wheels[0].cb(kClient, hint, b.r.fake.wheels[0].user);
    b.seconds(1);
    CHECK(noted(b, "Build an animal bed (the straw bed for farm animals) in your base"));
    three_beasts(b);  // a world that has had a stable; Tusk is stored
    at_stable(b);
    b.seconds(1);
    b.r.fake.wheels.clear();
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    CHECK(index_of(b.r.fake.wheels[0].items, "Stable: animal bed") < 0);
    CHECK(b.reply("beastmaster.call Tusk") ==
          "Tusk is in the stable: make it active at a stable (an animal bed in your base).");
}

// The beasts pack's Stable and Large Stable (tools/models/packs/beastmaster_stable.lua): their render models of their
// own, and the Stable's item.
constexpr const char* kOwnStableModel = "1a49170a-b3d2-498d-9062-8946f6469613";
constexpr const char* kLargeOwnStableModel = "5879f304-0050-4ecb-96f4-5cc0e66155d9";
constexpr uint32_t kStableItem = 0x5AB1E0u;

TEST_CASE("stable: the beasts pack's Stable (its own model) in a base is a stable named Stable") {
    BeastRig b;
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 5, 10, 0, kOwnStableModel);
    b.seconds(32);  // the next search of the world
    CHECK(b.reply("beastmaster.stable list").find("1. Stable at 5, 10, 0 (5 m from you)") != std::string::npos);
}

TEST_CASE("stable: the beasts pack's Large Stable (its own model) in a base is a stable named Large Stable") {
    BeastRig b;
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 5, 10, 0, kLargeOwnStableModel);
    b.seconds(32);
    CHECK(b.reply("beastmaster.stable list").find("1. Large Stable at 5, 10, 0 (5 m from you)") != std::string::npos);
}

TEST_CASE("stable: with the beasts pack's Stable item the hints name the Stable crafted at the Workbench, besides the "
          "animal bed") {
    BeastRig b;
    b.r.fake.item_names[kStableItem] = "Prop_Beastmaster_Stable";
    CHECK(b.reply("beastmaster.stable list") == "No stable in this world yet: build a Stable or a Large Stable (crafted "
                                                 "at the Workbench) or an animal bed in your base, or mark a prop with "
                                                 "beastmaster.stable mark.");
    b.reply("beastmaster.give wolf");
    press(b, VK_F3);
    REQUIRE(b.r.fake.wheels.size() == 1);
    CHECK(index_of(b.r.fake.wheels[0].items, "Stable: animal bed") < 0);
    const int hint = index_of(b.r.fake.wheels[0].items, "Stable: Workbench");
    REQUIRE(hint >= 0);
    b.r.fake.wheels[0].cb(kClient, hint, b.r.fake.wheels[0].user);
    b.seconds(1);
    CHECK(noted(b, "Build a Stable or a Large Stable (crafted at the Workbench) or an animal bed in your base"));
    three_beasts(b);  // Tusk is stored
    at_stable(b);
    b.seconds(1);
    CHECK(b.reply("beastmaster.call Tusk") ==
          "Tusk is in the stable: make it active at a stable (a Stable, a Large Stable or an animal bed in your base).");
}

TEST_CASE("stable: without the beasts pack the Stable's item is looked up at most once a minute (the wheel is built every "
          "second while the world has no stable)") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.seconds(130);
    CHECK(b.r.fake.item_lookups["Prop_Beastmaster_Stable"] >= 1);
    CHECK(b.r.fake.item_lookups["Prop_Beastmaster_Stable"] <= 4);
}

TEST_CASE("stable: a player whose level cannot be read yet keeps their beasts out (no slot trimming on a level of 0)") {
    BeastRig b;
    set_level(b, 1, 25);  // three slots
    write(b.data, "b 0x00000000000000aa wolf 0 1 Fang\nb 0x00000000000000aa boar 0 1 Tusk\n"
                  "b 0x00000000000000aa hound 0 1 Rex\n");
    b.spawnable(3);
    b.r.fake.components.erase({ecs::Level::kHash, 1});  // not there yet (a player who just joined or loaded)
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(3);
    CHECK(b.r.fake.spawns.size() == 3);
    CHECK(b.reply("beastmaster.list").find("goes home") == std::string::npos);
    set_level(b, 1, 25);
    b.seconds(2);
    CHECK(b.reply("beastmaster.list").find("3. Rex - hound, loyalty 1, out") != std::string::npos);
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
    CHECK(list.find("Wolf - wolf, loyalty 1, at home") != std::string::npos);
    CHECK(list.find("Boar - boar, loyalty 1, out") != std::string::npos);
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
TEST_CASE("beastmaster: a called bear's health adder keeps its maximum at its role, 2.2x the game's base, also after a level change; a wolf at 1.2x") {
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
    CHECK(bear.dataStorage[3] == 41);  // 34 x 1.2 = 40.8: 75 = 2.2 x 34 (a tamed bear's role, not the wild 2.5)
    bear.dataStorage[2] = 65;          // the game rescaled it to a new level
    b.seconds(2);
    CHECK(bear.dataStorage[3] == 78);  // 65 x 1.2
    wolf.dataStorage[2] = 34;
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(wolf.dataStorage[3] == 7);  // 34 x 0.2 = 6.8: the wolf's role, 1.2x
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
    b.r.fake.item_names[kLeanMeat] = "Food_T1_raw_bad_meat_LeanMeat";
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
    CHECK(b.reply("beastmaster.list").find("1. Bear - bear, loyalty 1, out") != std::string::npos);
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
    CHECK(b.reply("beastmaster.list").find("1. Bear - bear, loyalty 1, out") != std::string::npos);
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
    b.r.fake.item_names[kLeanMeat] = "Food_T1_raw_bad_meat_LeanMeat";
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
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, loyalty 1, out") != std::string::npos);  // not at the first look
    b.seconds(2);
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, loyalty 1, at home") != std::string::npos);
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
    CHECK(b.reply("beastmaster.list").find("1. Aurochs - aurochs, loyalty 1, out") != std::string::npos);
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
    CHECK(h.dataStorage[3] == 40);  // 40 + 40 = 80 = 2 x 40: a tamed aurochs' role (a wild one has 3x)
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
    CHECK(b.reply("beastmaster.list").find("Aurochs - aurochs, loyalty 1, at home") != std::string::npos);
    CHECK(b.reply("beastmaster.call Aurochs").find("pack") != std::string::npos);
}

TEST_CASE("beastmaster: a pack beast that cannot come says it comes with the EMF creature packs and how to install them") {
    BeastRig b;
    b.reply("beastmaster.give bear");
    b.r.fake.spawn_fails = true;  // no bear in this game: no pack, or its content was not built
    const std::string call = b.reply("beastmaster.call Bear");
    CHECK(call.find("EMF creature packs") != std::string::npos);
    CHECK(call.find("Install them with EMF.exe") != std::string::npos);
    CHECK(call.find("need EML") == std::string::npos);  // EMF builds the packs itself (0.4.0)
    CHECK(call.find("Install EML") == std::string::npos);
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
    CHECK(note.find("EML") == std::string::npos);
}

TEST_CASE("wild beasts: with only the beasts pack installed, wild aurochs and snakes come (the missing bear is skipped)") {
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
        CHECK((aurochs || spawned == "d4c87337-8c53-41df-8488-8079046e1502"));  // the pack's aurochs or snake
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

TEST_CASE("beastmaster: a snake is its own graph (WildBeast): tamed, listed, called with the pack's template and its role's health") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kSnakeGraph + " 6 1") == "snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kSnakeGraph + " 6") == "snake");
    b.beast(50, kSnakeGraph, 0, 5, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.0f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the snake") != std::string::npos);
    b.seconds(4);
    CHECK(b.reply("beastmaster.list").find("1. Snake - snake, loyalty 1, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call Snake").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kSnakeTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 8);  // 40 x 0.2: a tamed snake's role, 1.2x (a wild one has 2x)
    CHECK(b.reply("beastmaster.give snake").find("snake") != std::string::npos);
}

TEST_CASE("wild beasts: wild snakes come with the beasts pack, one at a time") {
    BeastRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));
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

TEST_CASE("beastmaster: the rare snakes are their own graphs: tamed, called with their role's health, fed their baits") {
    BeastRig b;
    CHECK(b.reply(std::string("beastmaster.species ") + kCrimsonGraph + " 6 1.15") == "crimson_snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kGoldenGraph + " 6 1.25") == "golden_snake");
    CHECK(b.reply(std::string("beastmaster.species ") + kAlbinoGraph + " 6 1.35") == "albino_snake");
    CHECK(b.r.fake.settings["beastmaster.bait_crimson_snake"] == "Food_T2_raw_bad_meat_Game");
    CHECK(b.r.fake.settings["beastmaster.bait_golden_snake"] == "Food_T3_Turkey_Bait");
    CHECK(b.r.fake.settings["beastmaster.bait_albino_snake"] == "Food_T5_Yak_Bait");
    b.beast(50, kAlbinoGraph, 0, 5, kWildBeast, 20, 100);
    component<ecs::ScaleComponent>(b.r.fake, 50).uniformScale = 1.35f;
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the albino snake") != std::string::npos);
    b.seconds(4);
    CHECK(b.noted("You tamed an albino snake: Albino snake"));
    CHECK(b.reply("beastmaster.list").find("1. Albino snake - albino_snake, loyalty 1, out") != std::string::npos);
    b.reply("beastmaster.dismiss all");
    b.spawnable(1);
    b.seconds(11);
    CHECK(b.reply("beastmaster.call 1").find("comes") != std::string::npos);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns[0].guid) == kAlbinoTemplate);
    auto& h = component<ecs::Health>(b.r.fake, 1234);
    h.dataStorage[2] = 40;
    b.seconds(1);
    CHECK(h.dataStorage[3] == 8);  // 40 x 0.2: a tamed albino snake's role, 1.2x (a wild one has 5x)
    for (const char* id : {"crimson_snake", "golden_snake"}) CHECK(b.reply(std::string("beastmaster.give ") + id).find(id) != std::string::npos);
}

TEST_CASE("wild beasts: a wild snake is now and then a rare one: albino 0.25 %, golden king 0.4 %, crimson 1 % of them") {
    RollRig b({{"wild_bear_minutes", "1"}});
    b.r.fake.missing_templates.insert(guid_bytes(kBearTemplate));
    b.r.fake.missing_templates.insert(guid_bytes(kAurochsTemplate));
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
    for (const char* t : {kBearTemplate, kAurochsTemplate, kCrimsonTemplate}) old.r.fake.missing_templates.insert(guid_bytes(t));
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
    CHECK(b.reply("beastmaster.list").find("Aurochs - aurochs, loyalty 1, out") != std::string::npos);
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
    b.r.fake.item_names[kLeanMeat] = "Food_T1_raw_bad_meat_LeanMeat";
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
    b.r.fake.item_names[kLeanMeat] = "Food_T1_raw_bad_meat_LeanMeat";
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
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, loyalty 1, aside") != std::string::npos);  // still out: it comes back
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
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, loyalty 1, out") != std::string::npos);
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
    CHECK(list.find("Wolf - wolf, loyalty 1, aside") != std::string::npos);
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
    CHECK(b.reply("beastmaster.list").find("Wolf - wolf, loyalty 1, out") != std::string::npos);
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
                                          ", plural = \"trolls\", factions = { [3] = true }, health_mul = 3, role_health = 3") + "}\n");
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 3") == "troll");
    CHECK(b.reply(std::string("beastmaster.species ") + kTrollGraph + " 6") == "unknown");  // not its faction
    CHECK(b.reply(std::string("beastmaster.species ") + kWolfGraph + " 6") == "wolf");       // the mod's own stay
    b.beast(50, kTrollGraph, 0, 5, kVukah, 20, 100);
    b.look(1, 0, 10.5, 0, 0);
    CHECK(b.reply("beastmaster.tame").find("Taming the troll") != std::string::npos);
    b.seconds(4);
    CHECK(b.noted("You tamed a troll: Troll"));
    CHECK(b.reply("beastmaster.list").find("1. Troll - troll, loyalty 1, out") != std::string::npos);
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
    for (const char* t : {kBearTemplate, kAurochsTemplate, kSnakeTemplate}) b.r.fake.missing_templates.insert(guid_bytes(t));
    no_rare_snakes(b.r.fake);
    extra_species(b, "return {\n" + entry("troll", "Troll", kTrollTemplate, kTrollGraph, ", pack = \"a private pack\"") + "}\n");
    b.spawnable(2);
    b.seconds(61);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(guid_text(b.r.fake.spawns.back().guid) == kTrollTemplate);
}

// ---- talents: points from loyalty, spent at a stable, kept with the world ----
namespace {
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
// Player 2's wolves Grey and Wolf 2 at loyalty 4 (60 points: 3 talent points each) from a save; a built stable 3 m from
// player 2 (at 30, 10, 0).
void guest_wolves(BeastRig& b) {
    write(b.data, "b 0x00000000000000bb wolf 0 0 Grey\nb 0x00000000000000bb wolf 0 0 Wolf 2\n"
                  "y 0x00000000000000bb 60 Grey\ny 0x00000000000000bb 60 Wolf 2\ns 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    flame_altar(b, 900, 30, 0);
    stable_prop(b, 961, 33, 10, 0);
    b.seconds(32);  // the next search of the world finds it
}
}  // namespace

TEST_CASE("talent: a beast gets a point for each loyalty above 1; the new loyalty's note says so") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    CHECK(has(b.reply("beastmaster.talents Wolf"), "Wolf's talents: no points yet (one for each loyalty above 1)"));
    for (uint32_t i = 0; i < 10; ++i) {
        creature(b, 100 + i, 0, 10, kSkeletonFoe);
        deliver(b.r.fake, "HitEvent", hit_event(1234, 100 + i));
        deliver(b.r.fake, "EntityDiedEvent", died_event(100 + i));
        b.seconds(1);
    }
    CHECK(b.noted("Wolf reached loyalty 2: a talent point to spend at a stable."));
    const std::string list = b.reply("beastmaster talents Wolf");
    CAPTURE(list);
    CHECK(has(list, "Wolf's talents: 1 point (1 left)"));
    for (const char* line : {"Strength 0/5: +4 % damage a point", "Toughness 0/5: +5 % health a point",
                             "Swiftness 0/3: +4 % speed a point",
                             "Second Wind 0/1: a killing blow leaves it at 20 % health, then 5 min before it works again",
                             "Ambush 0/2: +50 % damage a point on its first hit in a fight",
                             "Guard 0/2: no damage for the first 3 s of a fight (5 s with 2 points), once every 30 s",
                             "Menace 0/3: draws 10 % more of the monsters a point",
                             "Taunt 0/2: roars when a monster near it hits you: the monsters within 10 m turn on it for 6 s, "
                             "every 45 s (30 s with 2 points)"})
        CHECK(has(list, line));
    b.reply("beastmaster.loyalty Wolf 10");
    CHECK(has(b.reply("beastmaster.talents 1"), "Wolf's talents: 9 points (9 left)"));
}

TEST_CASE("talent: spending and reset at a stable; refused away from one, when full or when no point is left") {
    BeastRig b;
    guest_wolves(b);
    auto say = [&](const std::string& c) { return b.reply(c, 2, 0xBB); };
    CHECK(say("beastmaster talent Grey strength") == "Grey: Strength 1/5, +4 % damage (2 points left).");
    CHECK(say("beastmaster.talent Grey Strength") == "Grey: Strength 2/5, +8 % damage (1 point left).");
    CHECK(say("beastmaster talent 1 STRENGTH") == "Grey: Strength 3/5, +12 % damage (0 points left).");
    CHECK(say("beastmaster talent Grey toughness") == "Grey has no talent point left (3 of 3 spent).");
    CHECK(has(say("beastmaster.talents Grey"), "Strength 3/5: +12 % damage"));
    CHECK(say("beastmaster talents Grey reset") == "Grey's talents are reset: 3 points to spend.");
    CHECK(has(say("beastmaster.talents Grey"), "Grey's talents: 3 points (3 left)"));
    CHECK(say("beastmaster talent Wolf 2 second wind") ==
          "Wolf 2: Second Wind 1/1, a killing blow leaves it at 20 % health, then 5 min before it works again "
          "(2 points left).");
    CHECK(say("beastmaster talent Wolf 2 Second_Wind") == "Wolf 2's Second Wind is full (1/1).");
    CHECK(say("beastmaster talent Wolf 2 guard") ==
          "Wolf 2: Guard 1/2, no damage for the first 3 s of a fight, once every 30 s (1 point left).");
    const std::string which = "A talent is Strength, Toughness, Swiftness, Second Wind, Ambush, Guard, Menace or Taunt: "
                              "/beastmaster talent "
                              "<name or number> <talent>";
    CHECK(say("beastmaster talent Grey shield") == which);
    CHECK(say("beastmaster talent Grey") == which);
    CHECK(say("beastmaster talent Nobody strength") == "No such beast (beastmaster.list shows yours).");
    CHECK(say("beastmaster talents Nobody") == "No such beast (beastmaster.list shows yours).");
    place(b.r.fake, 2, 60, 10, 0);  // 27 m from the stable
    CHECK(say("beastmaster talent Grey ambush") == "Go to a stable (an animal bed in your base) within 10 m to do that.");
    CHECK(say("beastmaster talents Grey reset") == "Go to a stable (an animal bed in your base) within 10 m to do that.");
    CHECK(has(say("beastmaster talents Grey"), "Grey's talents: 3 points (3 left)"));  // a list anywhere
}

TEST_CASE("talent: spent points are kept with the world; a release forgets them, a rename keeps them; 1.6.0 and 1.2.0 saves load with none spent") {
    BeastRig b;
    // a Beastmaster 1.6.0 save: modes, beasts, loyalty, active flags, stabled, a mark
    write(b.data, "m 0x00000000000000aa passive\nb 0x00000000000000aa wolf 0 0 Fang\nb 0x00000000000000aa boar 0 0 Tusk\n"
                  "y 0x00000000000000aa 60 Fang\ny 0x00000000000000aa 30 Tusk\na 0x00000000000000aa Fang\ns 1\n"
                  "k 50.00 10.00 50.00 -\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    const std::string list = b.reply("beastmaster.list");
    CHECK(has(list, "1. Fang - wolf, loyalty 4, at home, active"));
    CHECK(has(list, "2. Tusk - boar, loyalty 3, at home"));
    CHECK(has(b.reply("beastmaster.talents Fang"), "Fang's talents: 3 points (3 left)"));
    at_stable(b);
    b.reply("beastmaster.talent Fang strength");
    b.reply("beastmaster.talent Fang strength");
    CHECK(b.reply("beastmaster.talent Fang guard") ==
          "Fang: Guard 1/2, no damage for the first 3 s of a fight, once every 30 s (0 points left).");
    b.seconds(1);
    const std::string saved = b.saved();
    CAPTURE(saved);
    for (const char* line : {"t 0x00000000000000aa strength:2,guard:1 Fang", "m 0x00000000000000aa passive",
                             "y 0x00000000000000aa 60 Fang", "y 0x00000000000000aa 30 Tusk", "a 0x00000000000000aa Fang",
                             "\ns 1", "k 50.00 10.00 50.00 -"})
        CHECK(has(saved, line));
    b.reply("beastmaster.rename Fang Rex");
    CHECK(has(b.saved(), "t 0x00000000000000aa strength:2,guard:1 Rex"));
    b.r.fake.session = 3;  // the world loads again
    fire_load(1);
    b.seconds(1);
    const std::string again = b.reply("beastmaster.talents Rex");
    CAPTURE(again);
    CHECK(has(again, "Rex's talents: 3 points (0 left)"));
    CHECK(has(again, "Strength 2/5: +8 % damage"));
    CHECK(has(again, "Guard 1/2: no damage for the first 3 s of a fight, once every 30 s"));
    b.reply("beastmaster.release Rex");
    CHECK(b.reply("beastmaster.release Rex") == "Rex is gone: you let it go.");
    CHECK_FALSE(has(b.saved(), "\nt "));
    // a Beastmaster 1.2.0 save: beasts only
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\n");
    b.r.fake.session = 4;
    fire_load(1);
    b.seconds(1);
    CHECK(has(b.reply("beastmaster.talents Fang"), "Fang's talents: no points yet"));
    b.reply("beastmaster.mode aggressive");
    b.seconds(1);
    CHECK(has(b.saved(), "b 0x00000000000000aa wolf 0 0 Fang"));
    CHECK_FALSE(has(b.saved(), "\nt "));
}

TEST_CASE("talent: points spent beyond loyalty stay; none can be spent; the list says how many too many") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.loyalty Wolf 4");
    at_stable(b);
    for (int i = 0; i < 3; ++i) b.reply("beastmaster.talent Wolf strength");
    CHECK(b.reply("beastmaster.loyalty Wolf 2") ==
          "Wolf's loyalty is now 2. Talents: 1 point (2 too many: reset to choose again).");
    const std::string list = b.reply("beastmaster.talents Wolf");
    CHECK(has(list, "Wolf's talents: 1 point (2 too many: reset to choose again)"));
    CHECK(has(list, "Strength 3/5: +12 % damage"));
    CHECK(b.reply("beastmaster.talent Wolf guard") == "Wolf has 2 talent points spent too many: reset to choose again.");
    b.seconds(1);
    CHECK(has(b.saved(), "t 0x00000000000000aa strength:3 Wolf"));
    CHECK(b.reply("beastmaster.talents Wolf reset") == "Wolf's talents are reset: 1 point to spend.");
}

TEST_CASE("talent: t lines from another version: an unknown talent is skipped, a rank over the most is cut to it, a name with no beast is ignored") {
    BeastRig b;
    write(b.data, "b 0x00000000000000aa wolf 0 0 Fang\ny 0x00000000000000aa 450 Fang\n"
                  "t 0x00000000000000aa strength:9,shield:2,guard:1 Fang\nt 0x00000000000000aa ambush:1 Nobody\n"
                  "t 0x00000000000000aa nonsense Fang\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
    const std::string list = b.reply("beastmaster.talents Fang");
    CAPTURE(list);
    CHECK(has(list, "Fang's talents: 9 points (3 left)"));
    CHECK(has(list, "Strength 5/5: +20 % damage"));
    CHECK(has(list, "Guard 1/2"));
    b.reply("beastmaster.mode passive");  // a change: the world's data is written
    b.seconds(1);
    const std::string saved = b.saved();
    CAPTURE(saved);
    CHECK(has(saved, "t 0x00000000000000aa strength:5,guard:1 Fang"));
    CHECK_FALSE(has(saved, "Nobody"));
    CHECK_FALSE(has(saved, "shield"));
}

// ---- talents on a beast out: Strength, Toughness, Swiftness ----
namespace {
// Movement's storage [1] (C index): its multiplier, which Swiftness multiplies (mods/beastmaster/talents.lua
// SPEED_ENTRIES).
float speed_of(BeastRig& b, uint32_t id) { return now_of<ecs::Movement>(b, id).dataStorage[1]; }
void one_step(BeastRig& b) {
    b.r.fake.tick += 1;
    b.host->tick(kServer);
}
}  // namespace

TEST_CASE("talent: Strength and Toughness go on top of role and loyalty, at once on a beast out and on the next call") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.loyalty Wolf 3");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    at_stable(b);
    b.seconds(1);
    const double f = 1 + 2.0 / 9 * 0.2;  // loyalty 3
    REQUIRE(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f));
    REQUIRE(now_of<ecs::Health>(b, 1234).dataStorage[3] == 25);  // 100 x 1.2 x f - 100, rounded
    b.reply("beastmaster.talent Wolf strength");
    b.reply("beastmaster.talent Wolf strength");
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f * 1.08));  // at once
    b.reply("beastmaster.talents Wolf reset");
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f));
    b.reply("beastmaster.talent Wolf toughness");
    b.reply("beastmaster.talent Wolf strength");
    one_step(b);
    CHECK(now_of<ecs::Health>(b, 1234).dataStorage[3] == 32);  // 100 x 1.2 x f x 1.05 - 100, rounded
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f * 1.04));
    b.reply("beastmaster.dismiss Wolf");
    next_beast(b, 1235);
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(now_of<ecs::Health>(b, 1235).dataStorage[3] == 32);
    CHECK(now_of<ecs::DamageMod>(b, 1235).dataStorage[2] == doctest::Approx(1.15 * f * 1.04));
}

TEST_CASE("talent: Swiftness multiplies the Movement multiplier, kept when the game writes it again; reset gives the game's value back; no Swiftness leaves Movement alone") {
    BeastRig b;
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.loyalty Wolf 4");
    next_beast(b, 1234);
    component<ecs::Movement>(b.r.fake, 1234).dataStorage[1] = 1.0f;
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    at_stable(b);
    b.seconds(1);
    CHECK(speed_of(b, 1234) == 1.0f);  // no Swiftness yet: as the game made it
    for (int i = 0; i < 3; ++i) b.reply("beastmaster.talent Wolf swiftness");
    CHECK(speed_of(b, 1234) == doctest::Approx(1.12));  // at once
    live_of<ecs::Movement>(b, 1234).dataStorage[1] = 0.5f;  // the game writes it anew
    one_step(b);
    CHECK(speed_of(b, 1234) == doctest::Approx(0.56));
    b.seconds(2);
    CHECK(speed_of(b, 1234) == doctest::Approx(0.56));  // not again on top of itself
    b.reply("beastmaster.talents Wolf reset");
    CHECK(speed_of(b, 1234) == doctest::Approx(0.5));
    live_of<ecs::Movement>(b, 1234).dataStorage[1] = 0.8f;  // the game's own from now on
    b.seconds(2);
    CHECK(speed_of(b, 1234) == doctest::Approx(0.8));
    next_beast(b, 1235);
    component<ecs::Movement>(b.r.fake, 1235).dataStorage[1] = 0.7f;
    b.seconds(11);
    REQUIRE(b.reply("beastmaster.call Boar").find("comes") != std::string::npos);
    b.seconds(2);
    CHECK(speed_of(b, 1235) == doctest::Approx(0.7));
}

// ---- talents in fights: Guard, Second Wind, Ambush ----
namespace {
// A hit from `source` on `target` that changed its health by `change` (negative: damage).
std::vector<uint8_t> hit(uint32_t source, uint32_t target, int32_t change) {
    auto e = hit_event(source, target);
    std::memcpy(&e[offsetof(ecs::HitEvent, healthChange)], &change, 4);
    return e;
}
// The game's minimum health of `id` (Health storage [1]): the floor Guard and Second Wind hold.
int32_t floor_of(BeastRig& b, uint32_t id) { return now_of<ecs::Health>(b, id).dataStorage[1]; }
// Player 1's wolf (loyalty 10: 9 points) out as entity 1234 with 100 of 100 health, at a stable, with the talents
// `spend` spent; foe 70 (a skeleton, 100 of 100) beside it.
void wolf_with(BeastRig& b, const std::vector<std::string>& spend) {
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.loyalty Wolf 10");
    next_beast(b, 1234);
    component<ecs::Movement>(b.r.fake, 1234).dataStorage[1] = 1.0f;
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234).guardEntityId.id = 1;  // it guards its owner
    at_stable(b);
    for (const auto& t : spend) REQUIRE(b.reply("beastmaster.talent Wolf " + t).rfind("Wolf: ", 0) == 0);
    creature(b, 70, 0, 10, kSkeletonFoe);
    b.seconds(1);
    b.r.fake.attr_adds.clear();
    b.r.fake.damages.clear();
}
}  // namespace

TEST_CASE("talent: Guard: the first seconds of a fight take no damage, the opening hit healed back; a hit after 10 s quiet starts a new fight") {
    BeastRig b;
    wolf_with(b, {"guard"});
    REQUIRE(floor_of(b, 1234) == 0);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 70;  // the foe's opening hit: 30
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -30));
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 30));  // healed back
    CHECK(floor_of(b, 1234) == 100);            // its health when the fight started
    b.seconds(2);
    b.r.fake.attr_adds.clear();
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -10));  // in the fight: the floor holds, nothing healed back
    CHECK(b.r.fake.attr_adds.empty());
    CHECK(floor_of(b, 1234) == 100);
    b.seconds(2);  // 4 s after the fight started: 3 s of Guard are over
    CHECK(floor_of(b, 1234) == 0);
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -10));  // still the same fight (2 s after the last hit)
    CHECK(floor_of(b, 1234) == 0);
    b.reply("beastmaster.talent Wolf guard");  // 2 points: 5 s, from the next fight
    b.seconds(27);  // the fight is over (10 s quiet), and Guard worked 31 s ago (at most once every 30 s)
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 60;
    b.r.fake.attr_adds.clear();
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -10));  // its own hit starts the next fight
    CHECK(b.r.fake.attr_adds.empty());
    CHECK(floor_of(b, 1234) == 60);
    b.seconds(4);
    CHECK(floor_of(b, 1234) == 60);
    b.seconds(2);
    CHECK(floor_of(b, 1234) == 0);
}

TEST_CASE("talent: Second Wind: a killing blow leaves it at 20 %; then 5 minutes before it works again") {
    BeastRig b;
    wolf_with(b, {"second wind"});
    CHECK(floor_of(b, 1234) == 1);  // ready: a killing blow leaves it at 1
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 1;
    one_step(b);
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 19));  // up to 20 of 100
    CHECK(notes_with(b.r.fake, "Wolf gets up again: Second Wind (5 min before it works again).") == 1);
    CHECK(floor_of(b, 1234) == 0);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 40;
    b.seconds(290);
    CHECK(floor_of(b, 1234) == 0);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 1;  // within the 5 minutes: nothing
    one_step(b);
    CHECK(notes_with(b.r.fake, "Wolf gets up again") == 1);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 40;
    b.seconds(11);
    CHECK(floor_of(b, 1234) == 1);  // ready again
}

TEST_CASE("talent: Ambush: the first damaging hit in a fight deals 50 % more a point; a bonus that kills is its owner's") {
    BeastRig b;
    wolf_with(b, {"ambush"});
    using Hit = std::tuple<uint32_t, uint32_t, int32_t>;
    b.r.fake.attrs[{70, EMF_ATTR_HEALTH}] = 60;
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -40));
    REQUIRE(b.r.fake.damages.size() == 1u);
    CHECK(b.r.fake.damages[0] == Hit{1234, 70, 20});
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -10));  // its next hit: nothing more
    CHECK(b.r.fake.damages.size() == 1u);
    b.reply("beastmaster.talent Wolf ambush");
    b.seconds(11);  // 10 s quiet: the next hit starts a new fight
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -5));   // the foe hits first: the fight starts, Ambush waits
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -0));   // a hit that deals nothing: still waits
    CHECK(b.r.fake.damages.size() == 1u);
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -20));
    REQUIRE(b.r.fake.damages.size() == 2u);
    CHECK(b.r.fake.damages[1] == Hit{1234, 70, 20});  // 2 points: +100 %
    b.seconds(11);
    creature(b, 71, 0, 12, kSkeletonFoe);
    b.r.fake.attrs[{71, EMF_ATTR_HEALTH}] = 26;  // 26 of 100 after the hit: the bonus kills it
    deliver(b.r.fake, "HitEvent", hit(1234, 71, -26));
    REQUIRE(b.r.fake.damages.size() == 3u);
    CHECK(b.r.fake.damages[2] == Hit{1, 71, 26});  // the owner's: the XP and loot are theirs
}

TEST_CASE("talent: Ambush never hits a player or a player's ally, nor a foe its hit already handed to the owner") {
    BeastRig b;
    wolf_with(b, {"ambush"});
    using Hit = std::tuple<uint32_t, uint32_t, int32_t>;
    creature(b, 80, 5, 5, kPlayerAlly);  // another player's beast
    deliver(b.r.fake, "HitEvent", hit(1234, 2, -10));
    deliver(b.r.fake, "HitEvent", hit(1234, 80, -10));
    CHECK(b.r.fake.damages.empty());
    b.r.fake.attrs[{70, EMF_ATTR_HEALTH}] = 20;  // 20 of 100: the XP credit hands the killing blow to the owner
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -30));
    REQUIRE(b.r.fake.damages.size() == 1u);
    CHECK(b.r.fake.damages[0] == Hit{1, 70, 20});
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -10));  // its first damaging hit in this fight was spent
    CHECK(b.r.fake.damages.size() == 1u);
}

TEST_CASE("talent: guard and second wind together: the higher floor while Guard lasts, then Second Wind's, then the game's") {
    BeastRig b;
    wolf_with(b, {"guard", "second wind"});
    CHECK(floor_of(b, 1234) == 1);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 80;
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -20));
    CHECK(floor_of(b, 1234) == 100);
    b.seconds(4);
    CHECK(floor_of(b, 1234) == 1);  // Guard is over; Second Wind is ready
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 1;
    one_step(b);
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 19));
    CHECK(floor_of(b, 1234) == 0);  // neither: the game's minimum
}

TEST_CASE("talent: a beast sent home mid-fight leaves nothing behind; its next call starts a fresh fight") {
    BeastRig b;
    wolf_with(b, {"guard", "second wind", "swiftness"});
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 90;
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -10));
    REQUIRE(floor_of(b, 1234) == 100);
    REQUIRE(speed_of(b, 1234) == doctest::Approx(1.04));
    b.reply("beastmaster.dismiss Wolf");
    // the game gives the id to another creature: nothing of the wolf's is written on it
    component<ecs::Health>(b.r.fake, 1234);
    component<ecs::Movement>(b.r.fake, 1234).dataStorage[1] = 1.0f;
    b.seconds(5);
    CHECK(floor_of(b, 1234) == 0);
    CHECK(speed_of(b, 1234) == 1.0f);
    next_beast(b, 1235);
    b.seconds(6);
    REQUIRE(b.reply("beastmaster.call Wolf").find("comes") != std::string::npos);
    b.seconds(1);
    CHECK(floor_of(b, 1235) == 1);  // Second Wind ready
    b.seconds(19);                  // Guard worked 31 s ago (once every 30 s, also across a call)
    b.r.fake.attrs[{1235, EMF_ATTR_HEALTH}] = 90;
    b.r.fake.attr_adds.clear();
    deliver(b.r.fake, "HitEvent", hit(70, 1235, -10));  // the opening hit of its own fight
    CHECK(b.added(1235, EMF_ATTR_HEALTH, 10));
    CHECK(floor_of(b, 1235) == 100);
}

TEST_CASE("talent: a reset while out takes Second Wind's floor off at the next step") {
    BeastRig b;
    wolf_with(b, {"second wind"});
    REQUIRE(floor_of(b, 1234) == 1);
    b.reply("beastmaster.talents Wolf reset");
    one_step(b);
    CHECK(floor_of(b, 1234) == 0);
}

// ---- the beast's tab of the Beasts window ----
namespace {
// three_beasts with Fang at loyalty 3 (2 talent points) out as entity 1234, the host at a stable with the window open.
void fang_window(BeastRig& b) {
    three_beasts(b);
    b.reply("beastmaster.loyalty Fang 3");
    next_beast(b, 1234);
    REQUIRE(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);
    at_stable(b);
    b.seconds(1);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 1u);
}
bool lit(const Fake::Win& w, const std::string& key) { return (flags_at(w, key) & EMF_UI_SELECTED) != 0u; }
bool off(const Fake::Win& w, const std::string& key) { return (flags_at(w, key) & EMF_UI_DISABLED) != 0u; }
// The window's picture row, or -1.
int image_row(const Fake::Win& w) {
    for (size_t i = 0; i < w.widgets.size(); ++i)
        if (w.widgets[i].type == EMF_UI_IMAGE) return static_cast<int>(i);
    return -1;
}
// The icon of the window's picture row as last drawn (0: none).
uint32_t picture(BeastRig& b) {
    const int i = image_row(shown(b));
    return i < 0 ? 0u : shown(b).widgets[static_cast<size_t>(i)].icon;
}
}  // namespace

TEST_CASE("beast tab: the selected beast's portrait turns under its name (EMF's picture of its kind, loaded once)") {
    BeastRig b;
    with_images(b.r.api);  // a v26 core
    fang_window(b);
    CHECK(image_row(shown(b)) == -1);  // the Stable tab has none
    CHECK(b.r.fake.images_loaded.empty());
    click(b, "beast.Fang");
    {
        const Fake::Win& w = shown(b);
        const int pic = image_row(w);
        REQUIRE(pic > 0);
        CHECK(w.texts[static_cast<size_t>(pic - 1)] == "Fang");  // right under its name
        CHECK(w.widgets[static_cast<size_t>(pic)].icon == 50u);
        CHECK(w.widgets[static_cast<size_t>(pic)].fps == 8u);
        // 6 lines: with the picture the tab's last row (Make active, Store, Release) still fits a window of 85 % of a
        // 995 px high game window, as the tab with every talent is
        CHECK(w.widgets[static_cast<size_t>(pic)].height == doctest::Approx(6.0));
    }
    CHECK(b.r.fake.images_loaded == std::vector<std::string>{"portrait:beastmaster.wolf"});
    click(b, "tab.stable");
    CHECK(picture(b) == 0u);
    click(b, "tab.beast");
    CHECK(picture(b) == 50u);
    click(b, "beast.Tusk");  // the boar
    CHECK(picture(b) == 51u);
    click(b, "beast.Fang");
    CHECK(picture(b) == 50u);
    CHECK(b.r.fake.images_loaded == std::vector<std::string>{"portrait:beastmaster.wolf", "portrait:beastmaster.boar"});
}

TEST_CASE("beast tab: without its picture (not drawn, or a core before v26) the tab has no picture row and no gap") {
    for (bool v26 : {true, false}) {
        CAPTURE(v26);
        BeastRig b;
        if (v26) {
            with_images(b.r.api);
            b.r.fake.image_ok = false;  // the cache has no picture of it
        }
        fang_window(b);
        click(b, "beast.Fang");
        CHECK(image_row(shown(b)) == -1);
        CHECK(shows(shown(b), "Wolf, level 10"));
        CHECK(b.host->status(b.id) == "running");
        click(b, "tab.stable");
        click(b, "tab.beast");
        CHECK(b.r.fake.images_loaded.size() == (v26 ? 1u : 0u));  // a refused picture is not asked for again
    }
}

TEST_CASE("beastmaster: mod.json asks EMF for a portrait of every built-in kind (key beastmaster.<id>, its template)") {
    const std::string dir = std::string(EMF_SOURCE_DIR) + "/mods/beastmaster/";
    const std::string species = slurp(std::filesystem::path(dir + "species.lua").wstring());
    std::map<std::string, std::string> kinds;  // id -> template, from species.lua's list
    const std::regex entry(R"re(\{ id = "([a-z_]+)",[^\n]*template = "([0-9a-f-]{36})")re");
    for (auto it = std::sregex_iterator(species.begin(), species.end(), entry); it != std::sregex_iterator(); ++it)
        kinds[(*it)[1]] = (*it)[2];
    REQUIRE(kinds.size() == 10u);  // wolf, boar, sabretooth, hound, bear, aurochs and four snakes
    std::vector<std::string> problems;
    const auto items = emf::loader::read_portrait_lists(
        {{"beastmaster", slurp(std::filesystem::path(dir + "mod.json").wstring())}}, &problems);
    CHECK(problems.empty());
    CHECK(items.size() == kinds.size());
    for (const auto& [id, tpl] : kinds) {
        CAPTURE(id);
        const auto it = std::find_if(items.begin(), items.end(),
                                     [&](const emf::loader::PortraitItem& p) { return p.key == "beastmaster." + id; });
        REQUIRE(it != items.end());
        CHECK(it->template_guid == tpl);
        CHECK(it->model_guid.empty());
        CHECK(it->frames == 24);
        CHECK(it->size == 192);
    }
}

TEST_CASE("talent: selecting a beast opens its tab with its stats and talents; Stable goes back") {
    BeastRig b;  // level 10
    fang_window(b);
    CHECK(lit(shown(b), "tab.stable"));
    CHECK(row_at(shown(b), "tab.beast") == -1);  // nothing selected: the Stable tab alone
    click(b, "beast.Fang");
    const Fake::Win& w = shown(b);
    CHECK_FALSE(lit(w, "tab.stable"));
    CHECK(text_at(w, "tab.beast") == "Fang");
    CHECK(lit(w, "tab.beast"));
    CHECK(row_at(w, "active.1") == -1);  // the slots and the list are on the Stable tab
    CHECK(row_at(w, "beast.Tusk") == -1);
    for (const char* line : {"Wolf, level 10", "Loyalty 3: 30 kills to loyalty 4", "Health x1.25, damage x1.20, speed x1.00",
                             "Out, active", "Talents: 2 points (2 left)", "Strength 0/5: +4 % damage a point",
                             "Guard 0/2: no damage for the first 3 s of a fight (5 s with 2 points), once every 30 s"})
        CHECK_MESSAGE(shows(w, line), line);
    for (const char* t : {"talent.strength", "talent.toughness", "talent.swiftness", "talent.second_wind", "talent.ambush",
                          "talent.guard"})
        CHECK_MESSAGE(!off(w, t), t);
    CHECK(off(w, "reset_talents"));  // nothing spent
    CHECK(off(w, "make_active"));    // active already
    CHECK_FALSE(off(w, "store"));
    CHECK_FALSE(off(w, "release"));
    click(b, "tab.stable");
    CHECK(lit(shown(b), "tab.stable"));
    CHECK(shows(shown(b), "Active 1/2"));
    CHECK(lit(shown(b), "beast.Fang"));  // still selected
    CHECK(text_at(shown(b), "tab.beast") == "Fang");
    click(b, "tab.beast");
    CHECK(shows(shown(b), "Out, active"));
    click(b, "tab.stable");
    click(b, "beast.Tusk");  // another beast: its tab
    CHECK(text_at(shown(b), "tab.beast") == "Tusk");
    CHECK(shows(shown(b), "Boar, level 10"));
    CHECK(shows(shown(b), "Loyalty 1: 10 kills to loyalty 2"));
    CHECK(shows(shown(b), "At home, in the stable"));
    CHECK(shows(shown(b), "Talents: no points yet"));
    CHECK(off(shown(b), "talent.strength"));
}

TEST_CASE("talent: + spends a point at once on a beast out; full or no point left turns + off; Reset talents gives every point back") {
    BeastRig b;
    fang_window(b);
    const double f = 1 + 2.0 / 9 * 0.2;  // loyalty 3
    click(b, "beast.Fang");
    click(b, "talent.strength");
    CHECK(shows(shown(b), "Strength 1/5: +4 % damage"));
    CHECK(shows(shown(b), "Fang: Strength 1/5, +4 % damage (1 point left)."));
    CHECK(shows(shown(b), "Talents: 2 points (1 left)"));
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f * 1.04));
    click(b, "talent.second_wind");
    CHECK(shows(shown(b), "Second Wind 1/1: a killing blow leaves it at 20 % health, then 5 min before it works again (ready)"));
    CHECK(shows(shown(b), "Talents: 2 points (0 left)"));
    for (const char* t : {"talent.strength", "talent.second_wind", "talent.guard"}) CHECK_MESSAGE(off(shown(b), t), t);
    CHECK(b.saved().find("t 0x00000000000000aa strength:1,second_wind:1 Fang") != std::string::npos);
    CHECK_FALSE(off(shown(b), "reset_talents"));
    click(b, "reset_talents");
    CHECK(shows(shown(b), "Fang's talents are reset: 2 points to spend."));
    CHECK(shows(shown(b), "Talents: 2 points (2 left)"));
    CHECK(now_of<ecs::DamageMod>(b, 1234).dataStorage[2] == doctest::Approx(1.15 * f));
    CHECK(off(shown(b), "reset_talents"));
    b.reply("beastmaster.loyalty Fang 10");
    b.seconds(1);
    click(b, "talent.second_wind");
    CHECK(off(shown(b), "talent.second_wind"));  // full (1/1)
    CHECK_FALSE(off(shown(b), "talent.strength"));
}

TEST_CASE("talent: a beast released on its tab, or by chat while its tab is open, leaves the Stable tab showing") {
    BeastRig b;
    fang_window(b);
    click(b, "beast.Fang");
    click(b, "release");
    CHECK(shows(shown(b), "Click Release again within 5 s to let Fang go for good."));
    click(b, "release");
    CHECK(shows(shown(b), "Fang is gone: you let it go."));
    CHECK(lit(shown(b), "tab.stable"));
    CHECK(row_at(shown(b), "tab.beast") == -1);
    CHECK(shows(shown(b), "Stable 2/8"));
    click(b, "beast.Tusk");
    REQUIRE(text_at(shown(b), "tab.beast") == "Tusk");
    b.reply("beastmaster.release Tusk");  // the host's console, at the stable
    REQUIRE(b.reply("beastmaster.release Tusk") == "Tusk is gone: you let it go.");
    b.seconds(1);  // the window is drawn again
    CHECK(lit(shown(b), "tab.stable"));
    CHECK(row_at(shown(b), "tab.beast") == -1);
    CHECK(shows(shown(b), "Stable 1/8"));
}

// ---- talents: fixes after review ----
TEST_CASE("talent: an id that stops being our beast (its faction changes) keeps no floor or speed of ours") {
    BeastRig b;
    wolf_with(b, {"second wind", "swiftness"});
    REQUIRE(floor_of(b, 1234) == 1);
    REQUIRE(speed_of(b, 1234) == doctest::Approx(1.04));
    component<ecs::Faction>(b.r.fake, 1234).faction = static_cast<emf::gen::FactionType>(kSkeletonFoe);
    one_step(b);  // never written on a creature that is not the players' ally, and given back at once
    CHECK(floor_of(b, 1234) == 0);
    CHECK(speed_of(b, 1234) == doctest::Approx(1.0));
    b.seconds(2);  // the check that drops it
    CHECK(has(b.reply("beastmaster.list"), "1. Wolf - wolf, loyalty 10, at home"));
    CHECK(floor_of(b, 1234) == 0);
    CHECK(speed_of(b, 1234) == doctest::Approx(1.0));
}

TEST_CASE("talent: an id dropped because it guards someone else gets the game's minimum and speed back") {
    BeastRig b;
    wolf_with(b, {"second wind", "swiftness"});
    REQUIRE(floor_of(b, 1234) == 1);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234).guardEntityId.id = 999;  // another entity's guard now
    b.seconds(4);
    REQUIRE(has(b.reply("beastmaster.list"), "1. Wolf - wolf, loyalty 10, at home"));
    CHECK(floor_of(b, 1234) == 0);
    CHECK(speed_of(b, 1234) == doctest::Approx(1.0));
}

TEST_CASE("talent: a beast sent home that is no longer ours (not destroyed) gets the game's minimum and speed back") {
    BeastRig b;
    wolf_with(b, {"second wind", "swiftness"});
    component<ecs::Faction>(b.r.fake, 1234).faction = static_cast<emf::gen::FactionType>(kSkeletonFoe);
    b.reply("beastmaster.dismiss Wolf");  // before any step: it is left alive
    REQUIRE_FALSE(contains(b.r.fake.destroyed, 1234));
    b.seconds(2);
    CHECK(floor_of(b, 1234) == 0);
    CHECK(speed_of(b, 1234) == doctest::Approx(1.0));
}

TEST_CASE("talent: a floor or speed the game changed meanwhile is left as the game has it when the id leaves") {
    BeastRig b;
    wolf_with(b, {"second wind", "swiftness"});
    component<ecs::Faction>(b.r.fake, 1234).faction = static_cast<emf::gen::FactionType>(kSkeletonFoe);
    live_of<ecs::Health>(b, 1234).dataStorage[1] = 7;       // the game's own values now
    live_of<ecs::Movement>(b, 1234).dataStorage[1] = 0.6f;
    b.seconds(2);
    CHECK(floor_of(b, 1234) == 7);
    CHECK(speed_of(b, 1234) == doctest::Approx(0.6));
}

TEST_CASE("talent: Ambush is used up but strikes nothing when the first hit already killed the foe") {
    BeastRig b;
    wolf_with(b, {"ambush"});
    b.r.fake.attrs[{70, EMF_ATTR_HEALTH}] = 0;  // the bite killed it
    deliver(b.r.fake, "HitEvent", hit(1234, 70, -40));
    CHECK(b.r.fake.damages.empty());
    creature(b, 71, 0, 12, kSkeletonFoe);
    deliver(b.r.fake, "HitEvent", hit(1234, 71, -40));  // the same fight: it was used up
    CHECK(b.r.fake.damages.empty());
}

TEST_CASE("talent: Guard works at most once every 30 s") {
    BeastRig b;
    wolf_with(b, {"guard"});
    auto opening = [&]() {
        b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 70;
        b.r.fake.attr_adds.clear();
        deliver(b.r.fake, "HitEvent", hit(70, 1234, -30));
    };
    opening();
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 30));
    CHECK(floor_of(b, 1234) == 100);
    b.seconds(12);  // a new fight (10 s quiet), 12 s after Guard last worked
    opening();
    CHECK_FALSE(b.added(1234, EMF_ATTR_HEALTH, 30));
    CHECK(floor_of(b, 1234) == 0);
    b.seconds(19);  // 31 s after Guard last worked, 19 s quiet
    opening();
    CHECK(b.added(1234, EMF_ATTR_HEALTH, 30));
    CHECK(floor_of(b, 1234) == 100);
}

// ---- threat: how much of the monsters in a fight the beasts draw ----
TEST_CASE("threat: the beast tab says how much of the monsters a beast draws") {
    BeastRig b;
    fang_window(b);  // Fang, a wolf at loyalty 3 (2 points), out
    click(b, "beast.Fang");
    CHECK(shows(shown(b), "Draws 20 % of the monsters in a fight"));
    click(b, "talent.menace");
    CHECK(shows(shown(b), "Menace 1/3: draws 10 % more of the monsters"));
    CHECK(shows(shown(b), "Draws 30 % of the monsters in a fight"));
    click(b, "talent.taunt");
    CHECK(shows(shown(b), "Taunt 1/2: roars when a monster near it hits you: the monsters within 10 m turn on it for 6 s, "
                          "every 45 s"));
    click(b, "tab.stable");
    click(b, "beast.Tusk");  // a boar: a tank
    CHECK(shows(shown(b), "Draws 50 % of the monsters in a fight"));
    b.reply("beastmaster.mode passive");
    b.seconds(1);
    CHECK(shows(shown(b), "Draws no monsters (passive mode)"));
}

namespace {
// What monster `id` goes for now (SelectedTargets.targets[0]), and whether the game may switch its target.
uint32_t pick_of(BeastRig& b, uint32_t id) { return now_of<ecs::SelectedTargets>(b, id).targets[0].targetEntityId.id; }
uint32_t root_of(BeastRig& b, uint32_t id) { return now_of<ecs::SelectedTargets>(b, id).targets[0].targetRootEntityId.id; }
bool switches(BeastRig& b, uint32_t id) { return now_of<ecs::DynamicAggro>(b, id).allowTargetSwitches; }
// The game picks `pick` for monster `id` (0: nothing).
void game_picks(BeastRig& b, uint32_t id, uint32_t pick) {
    auto& t = component<ecs::SelectedTargets>(b.r.fake, id).targets[0];
    t.targetEntityId.id = pick;
    t.targetRootEntityId.id = pick;
}
// A hostile monster (a skeleton unless `faction`) at (x, 10, z), 100 of 100, that goes for `pick` (0: nothing), its
// target switches on.
void monster(BeastRig& b, uint32_t id, double x, double z, uint32_t pick, int faction = kSkeletonFoe) {
    creature(b, id, x, z, faction);
    component<ecs::DynamicAggro>(b.r.fake, id).allowTargetSwitches = true;
    game_picks(b, id, pick);
}
// Calls player 1's beast `name` (in the collection already) as entity `id` and puts it at (x, 10, z), guarding them.
void out_at(BeastRig& b, const std::string& name, uint32_t id, double x, double z) {
    next_beast(b, id);
    REQUIRE(b.reply("beastmaster.call " + name).find("comes") != std::string::npos);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, id).guardEntityId.id = 1;
    place(b.r.fake, id, x, 10, z);
}
size_t on(BeastRig& b, std::initializer_list<uint32_t> monsters, uint32_t target) {
    size_t n = 0;
    for (uint32_t m : monsters) n += pick_of(b, m) == target;
    return n;
}
}  // namespace

TEST_CASE("threat: a monster that goes for you joins the fight and is held on a beast by its share for at least 8 s, then picks again; one left to you is never written on") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    monster(b, 70, 10, 2, 1);  // as far from you as from the boar, going for you
    b.roll(0.6);               // over your half: the boar
    b.seconds(1);              // the next look at the fights
    CHECK(pick_of(b, 70) == 1234);
    CHECK(root_of(b, 70) == 1234);
    CHECK_FALSE(switches(b, 70));
    game_picks(b, 70, 1);  // the game picks you again
    one_step(b);
    CHECK(pick_of(b, 70) == 1234);  // held, every step
    b.roll(0.4);
    b.seconds(6);
    CHECK(pick_of(b, 70) == 1234);
    b.seconds(2);  // 8 s: it picks again: you; the game's own pick, switches on
    CHECK(switches(b, 70));
    CHECK(has(b.reply("beastmaster.threat"), "70 on you"));
    game_picks(b, 70, 0);
    b.seconds(2);
    CHECK(pick_of(b, 70) == 0);  // nothing written
    CHECK(switches(b, 70));
}

TEST_CASE("threat: a monster that hits your beast, or that you or your beast hit, joins the fight") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);  // the boar
    for (uint32_t m : {71u, 72u, 73u, 74u}) monster(b, m, 6, 4, 0);
    deliver(b.r.fake, "HitEvent", hit(1234, 71, -5));
    deliver(b.r.fake, "HitEvent", hit(1, 72, -5));
    deliver(b.r.fake, "HitEvent", hit(73, 1234, -5));
    deliver(b.r.fake, "HitEvent", hit(74, 2, -5));  // player 2 has no beast out
    one_step(b);
    const std::string held = b.reply("beastmaster.threat");
    CAPTURE(held);
    CHECK(has(held, "71 on Boar"));
    CHECK(has(held, "72 on you"));  // the second of a fight: 1 in 4 from 2, rounded up
    CHECK(has(held, "73 on Boar"));
    CHECK_FALSE(has(held, "74 on"));
    for (uint32_t m : {71u, 73u}) {
        CAPTURE(m);
        CHECK(pick_of(b, m) == 1234);
        CHECK_FALSE(switches(b, m));
    }
    CHECK(pick_of(b, 74) == 0);
    CHECK(switches(b, 74));
}

TEST_CASE("threat: shares: a tank 50 %, another beast 20 %, Menace 10 % a point; your beasts at most 90 %, of a boss at most 35 %") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.loyalty Boar 10");
    b.reply("beastmaster.loyalty Wolf 10");
    at_stable(b);
    out_at(b, "Boar", 1234, 10, 20);  // you at (0, 10, 10), the monsters at (0, 10, 20): all three 10 m from them
    out_at(b, "Wolf", 1235, -10, 20);
    place(b.r.fake, 1, 0, 10, 10);
    uint32_t next = 70;
    auto pick_at = [&](double roll, bool boss = false) {
        const uint32_t m = next++;
        monster(b, m, 0, 20, 1);
        if (boss) component<ecs::Boss>(b.r.fake, m);
        b.roll(roll);
        b.seconds(1);
        const uint32_t got = pick_of(b, m);
        b.r.fake.attrs[{m, EMF_ATTR_HEALTH}] = 0;  // it falls: the next one fights alone
        b.seconds(1);
        return got;
    };
    // the boar 50 %, the wolf 20 %: you 30 %
    CHECK(pick_at(0.29) == 1u);
    CHECK(pick_at(0.31) == 1234u);
    CHECK(pick_at(0.79) == 1234u);
    CHECK(pick_at(0.81) == 1235u);
    place(b.r.fake, 1, 0, 10, 0);  // to the stable: Menace 3 on both (80 % and 50 %: 130 %, at most 90 %)
    for (int i = 0; i < 3; ++i) {
        REQUIRE(b.reply("beastmaster.talent Boar menace").rfind("Boar: ", 0) == 0);
        REQUIRE(b.reply("beastmaster.talent Wolf menace").rfind("Wolf: ", 0) == 0);
    }
    place(b.r.fake, 1, 0, 10, 10);
    CHECK(pick_at(0.09) == 1u);
    CHECK(pick_at(0.11) == 1234u);  // 90 % x 80/130 = 55.4 %
    CHECK(pick_at(0.64) == 1234u);
    CHECK(pick_at(0.66) == 1235u);
    // a boss: your beasts 35 % at most
    CHECK(pick_at(0.64, true) == 1u);
    CHECK(pick_at(0.66, true) == 1234u);  // 35 % x 80/130 = 21.5 %
    CHECK(pick_at(0.87, true) == 1235u);
}

TEST_CASE("threat: at least 1 in 4 monsters of a fight stays on you, rounded up from 2 monsters; a single one can go to a beast") {
    RollRig b({{"tank_share_pct", "90"}});
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);  // every pick by share: the boar
    monster(b, 70, 6, 4, 1);
    b.seconds(1);
    CHECK(pick_of(b, 70) == 1234);  // alone: by share
    monster(b, 71, 7, 4, 1);
    b.seconds(1);
    CHECK(on(b, {70, 71}, 1) == 1u);  // 2 monsters: 1 on you
    for (int i = 0; i < 2; ++i) monster(b, 72 + i, 8.0 + i, 4, 1);
    b.seconds(1);
    CHECK(on(b, {70, 71, 72, 73}, 1) == 1u);
    CHECK(on(b, {70, 71, 72, 73}, 1234) == 3u);
    monster(b, 74, -6, 4, 1);
    b.seconds(1);
    CHECK(on(b, {70, 71, 72, 73, 74}, 1) == 2u);  // 5: 2 on you
    for (int i = 0; i < 3; ++i) monster(b, 75 + i, -7.0 - i, 4, 1);
    b.seconds(1);
    CHECK(on(b, {70, 71, 72, 73, 74, 75, 76, 77}, 1) == 2u);
    monster(b, 80, 6, -4, 1);
    b.seconds(1);
    CHECK(on(b, {70, 71, 72, 73, 74, 75, 76, 77, 80}, 1) == 3u);  // 9: 3 on you
}

TEST_CASE("threat: a monster picks again at once when its beast is sent home, falls or is out of its reach; with no beast left it gets its own pick back") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.give wolf");
    out_at(b, "Boar", 1234, 0, 4);
    out_at(b, "Wolf", 1235, 0, -4);
    monster(b, 70, 10, 0, 1);
    b.roll(0.99);  // the last: the wolf, then the boar
    b.seconds(1);
    REQUIRE(pick_of(b, 70) == 1235);
    b.reply("beastmaster.dismiss Wolf");  // sent home
    one_step(b);
    CHECK(pick_of(b, 70) == 1234);
    place(b.r.fake, 1234, 0, 10, 40);  // the boar 40 m from the monster: out of its reach
    one_step(b);
    CHECK(switches(b, 70));  // no beast in reach: the game's own pick
    game_picks(b, 70, 1);
    b.seconds(2);
    CHECK(pick_of(b, 70) == 1);
    CHECK(switches(b, 70));
    place(b.r.fake, 1234, 0, 10, 4);  // back
    b.seconds(1);
    REQUIRE(pick_of(b, 70) == 1234);
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));  // it falls
    one_step(b);
    CHECK(switches(b, 70));
}

TEST_CASE("threat: never on a monster not in the fight, a player-side creature, in passive mode or with the setting off; a monster tamed meanwhile gets its own pick back") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);  // a pick would be the boar
    monster(b, 80, 8, 0, 0);               // not fighting
    monster(b, 81, 6, 0, 1, kPlayerAlly);  // on the players' side
    monster(b, 82, 25, 0, 2);              // fighting player 2, who has no beast out
    b.seconds(2);
    for (uint32_t m : {80u, 81u, 82u}) {
        CAPTURE(m);
        CHECK(switches(b, m));
    }
    CHECK(pick_of(b, 80) == 0);
    CHECK(pick_of(b, 81) == 1);
    CHECK(pick_of(b, 82) == 2);
    monster(b, 70, 6, 4, 1);
    b.seconds(1);
    REQUIRE(pick_of(b, 70) == 1234);
    component<ecs::Faction>(b.r.fake, 70).faction = static_cast<emf::gen::FactionType>(kPlayerAlly);  // tamed
    one_step(b);
    CHECK(switches(b, 70));
    game_picks(b, 70, 0);
    b.seconds(2);
    CHECK(pick_of(b, 70) == 0);
    monster(b, 71, 6, 4, 1);
    b.seconds(1);
    REQUIRE(pick_of(b, 71) == 1234);
    b.reply("beastmaster.mode passive");
    one_step(b);
    CHECK(switches(b, 71));
    monster(b, 72, 6, 4, 1);
    b.seconds(2);
    CHECK(pick_of(b, 72) == 1);
    CHECK(switches(b, 72));
    b.reply("beastmaster.mode defending");
    monster(b, 73, 6, 4, 1);
    b.seconds(1);
    REQUIRE(pick_of(b, 73) == 1234);
    b.r.fake.settings["beastmaster.threat"] = "false";
    one_step(b);
    CHECK(switches(b, 73));
    monster(b, 74, 6, 4, 1);
    b.seconds(2);
    CHECK(pick_of(b, 74) == 1);
    CHECK(switches(b, 74));
}

// ---- Taunt ----
namespace {
// Player 1's beasts `names` (given now) at loyalty 10 with Taunt spent `points` times, at a stable.
void taunters(BeastRig& b, std::initializer_list<const char*> kinds, int points = 1) {
    for (const char* k : kinds) b.reply(std::string("beastmaster.give ") + k);
    for (const char* name : {"Wolf", "Boar"}) b.reply(std::string("beastmaster.loyalty ") + name + " 10");
    at_stable(b);
    for (const char* k : kinds) {
        std::string name = k;
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
        for (int i = 0; i < points; ++i) REQUIRE(b.reply("beastmaster.talent " + name + " taunt").rfind(name + ": ", 0) == 0);
    }
}
}  // namespace

TEST_CASE("threat: Taunt: a monster hitting you within 10 m of the beast makes it roar: the fight's monsters within 10 m turn on it for 6 s; 45 s before it roars again, 30 s with 2 points") {
    RollRig b;
    taunters(b, {"wolf"});
    out_at(b, "Wolf", 1234, 0, 4);
    b.roll(0.0);                // every pick by share: you
    monster(b, 70, 3, 4, 1);    // 3 m from the wolf
    monster(b, 71, -4, 4, 1);   // 4 m from it
    monster(b, 72, 0, 10, 0);   // 6 m from it, not in the fight
    monster(b, 73, 0, -16, 1);  // 20 m from it
    b.seconds(1);
    REQUIRE(pick_of(b, 70) == 1);
    REQUIRE(pick_of(b, 71) == 1);
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));
    CHECK(notes_with(b.r.fake, "Wolf roars: the monsters turn on it.") == 1);
    one_step(b);
    CHECK(pick_of(b, 70) == 1234);
    CHECK(pick_of(b, 71) == 1234);
    CHECK(pick_of(b, 72) == 0);
    CHECK(switches(b, 72));
    CHECK(pick_of(b, 73) == 1);
    b.seconds(5);
    CHECK(pick_of(b, 70) == 1234);  // over the shares (they pick you)
    CHECK_FALSE(switches(b, 70));
    b.seconds(2);                   // 6 s: they pick as usual: you, the game's own pick
    CHECK(switches(b, 70));
    b.seconds(3);
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));  // 10 s after the roar
    CHECK(notes_with(b.r.fake, "Wolf roars") == 1);
    b.seconds(36);
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));  // 46 s after it
    CHECK(notes_with(b.r.fake, "Wolf roars") == 2);
    REQUIRE(b.reply("beastmaster.talent Wolf taunt").rfind("Wolf: Taunt 2/2", 0) == 0);
    b.seconds(20);
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));
    CHECK(notes_with(b.r.fake, "Wolf roars") == 2);
    b.seconds(11);  // 31 s after the last roar
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));
    CHECK(notes_with(b.r.fake, "Wolf roars") == 3);
}

TEST_CASE("threat: only the nearest ready beast roars; a beast too far does not and keeps its cooldown; none in passive mode") {
    RollRig b({{"call_cooldown_s", "0"}});
    taunters(b, {"wolf", "boar"});
    out_at(b, "Wolf", 1234, 0, 4);
    out_at(b, "Boar", 1235, 0, -8);
    b.roll(0.0);
    monster(b, 69, 25, 0, 1);  // more than 10 m from both
    monster(b, 70, 3, 6, 1);   // 4 m from the wolf, 14 m from the boar
    monster(b, 71, 0, -1, 1);  // 5 m from the wolf, 7 m from the boar
    monster(b, 72, 0, -5, 1);  // 9 m from the wolf, 3 m from the boar
    b.seconds(1);
    deliver(b.r.fake, "HitEvent", hit(69, 1, -10));
    CHECK(notes_with(b.r.fake, "roars") == 0);       // too far: neither roars, both stay ready
    deliver(b.r.fake, "HitEvent", hit(71, 1, -10));  // both ready and in reach: the nearer
    CHECK(notes_with(b.r.fake, "Wolf roars") == 1);
    CHECK(notes_with(b.r.fake, "Boar roars") == 0);
    deliver(b.r.fake, "HitEvent", hit(72, 1, -10));  // the wolf waits: the boar is the nearest ready one
    CHECK(notes_with(b.r.fake, "Wolf roars") == 1);
    CHECK(notes_with(b.r.fake, "Boar roars") == 1);
    one_step(b);
    CHECK(pick_of(b, 70) == 1234);  // the wolf's roar only
    CHECK(pick_of(b, 71) == 1235);  // the boar's roar came later
    CHECK(pick_of(b, 72) == 1235);
    b.seconds(46);
    b.reply("beastmaster.mode passive");
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));
    CHECK(notes_with(b.r.fake, "roars") == 2);
}

TEST_CASE("threat: a boss in the middle of an attack keeps its target until the attack ends (a roar, a join), then turns") {
    RollRig b;
    taunters(b, {"wolf"});
    out_at(b, "Wolf", 1234, 0, 4);
    b.roll(0.0);
    monster(b, 75, 2, 4, 1);
    component<ecs::Boss>(b.r.fake, 75);
    auto& attacking = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 75);
    attacking.state = emf::gen::enemy01::State::ControlledByAttack;
    monster(b, 76, -2, 4, 1);
    component<ecs::Boss>(b.r.fake, 76);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 76).state = emf::gen::enemy01::State::Combat;
    b.seconds(1);
    deliver(b.r.fake, "HitEvent", hit(75, 1, -10));
    CHECK(notes_with(b.r.fake, "Wolf roars") == 1);
    one_step(b);
    CHECK(pick_of(b, 75) == 1);     // mid-attack: it keeps you for now
    CHECK(pick_of(b, 76) == 1234);  // the other boss turns
    b.seconds(2);
    CHECK(pick_of(b, 75) == 1);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 75).state = emf::gen::enemy01::State::Combat;  // the attack ends
    one_step(b);
    CHECK(pick_of(b, 75) == 1234);  // within the roar's 6 s
    b.r.fake.attrs[{75, EMF_ATTR_HEALTH}] = 0;
    b.r.fake.attrs[{76, EMF_ATTR_HEALTH}] = 0;
    b.seconds(1);
    monster(b, 77, 0, 8, 1);  // a boss joins mid-attack, picked for the wolf
    component<ecs::Boss>(b.r.fake, 77);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 77).state = emf::gen::enemy01::State::ControlledByAttack;
    b.roll(0.99);
    b.seconds(1);
    CHECK(has(b.reply("beastmaster.threat"), "77 on Wolf"));
    CHECK(pick_of(b, 77) == 1);
    CHECK(switches(b, 77));
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 77).state = emf::gen::enemy01::State::Combat;
    one_step(b);
    CHECK(pick_of(b, 77) == 1234);
}

TEST_CASE("threat: a roar that turns no monster says nothing and keeps the cooldown") {
    RollRig b;
    taunters(b, {"wolf"});
    out_at(b, "Wolf", 1234, 0, 4);
    b.roll(0.0);
    creature(b, 79, 2, 4, kSkeletonFoe);  // a monster whose target cannot be held
    b.seconds(1);
    deliver(b.r.fake, "HitEvent", hit(79, 1, -10));
    CHECK(notes_with(b.r.fake, "roars") == 0);
    monster(b, 70, 2, 5, 1);
    b.seconds(1);
    deliver(b.r.fake, "HitEvent", hit(70, 1, -10));
    CHECK(notes_with(b.r.fake, "Wolf roars: the monsters turn on it.") == 1);
}

TEST_CASE("threat: monsters of your fight are let go as soon as your last beast is out of the world") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.0);  // you
    monster(b, 70, 10, 2, 1);
    b.seconds(1);
    REQUIRE(has(b.reply("beastmaster.threat"), "70 on you"));
    CHECK(switches(b, 70));  // left to you: never written on
    b.reply("beastmaster.dismiss Boar");
    one_step(b);
    CHECK(b.reply("beastmaster.threat") == "No monster is held.");
}

// ---- threat: after review ----
TEST_CASE("threat: a monster with no hit to or from your side for twice the hold (16 s) is let go; a fight that goes on keeps it") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);
    monster(b, 70, 6, 4, 1);
    monster(b, 71, 7, 4, 1);
    b.roll(0.99);
    b.seconds(1);
    REQUIRE(has(b.reply("beastmaster.threat"), "70 on Boar"));
    REQUIRE_FALSE(switches(b, 70));
    for (int i = 0; i < 3; ++i) {
        b.seconds(5);
        deliver(b.r.fake, "HitEvent", hit(1234, 71, -5));  // the boar keeps biting 71
    }
    b.seconds(1);   // 16 s
    CHECK_FALSE(switches(b, 70));
    one_step(b);    // over 16 s without a hit
    CHECK(switches(b, 70));
    CHECK_FALSE(has(b.reply("beastmaster.threat"), "70 on"));
    CHECK(has(b.reply("beastmaster.threat"), "71 on"));
    game_picks(b, 70, 0);  // the game picks something else
    b.seconds(2);
    CHECK(pick_of(b, 70) == 0);
}

TEST_CASE("threat: a monster that goes home, flees or idles is let go at once, and taken again only when it fights again") {
    RollRig b;
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);
    monster(b, 70, 6, 4, 1);
    auto& ai = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 70);
    ai.state = emf::gen::enemy01::State::Combat;
    b.seconds(1);
    REQUIRE_FALSE(switches(b, 70));
    using S = emf::gen::enemy01::State;
    for (S st : {S::Idle, S::ReturnHome, S::FleeHome, S::WaitAtHome, S::Flee}) {
        CAPTURE(static_cast<int>(st));
        live_of<ecs::DynamicEnemy01BehaviorState>(b, 70).state = st;
        one_step(b);
        CHECK(switches(b, 70));
        b.seconds(2);  // its pick is still the boar, but it is not fighting
        CHECK(switches(b, 70));
        live_of<ecs::DynamicEnemy01BehaviorState>(b, 70).state = S::Combat;
        b.seconds(1);
        CHECK_FALSE(switches(b, 70));
    }
}

TEST_CASE("threat: the cap holds after closeness: a boss with your bear 2 m from it and you 15 m away goes to the beasts 35 % of the time") {
    RollRig b;
    b.reply("beastmaster.give bear");
    out_at(b, "Bear", 1234, 0, 13);
    uint32_t next = 70;
    auto pick_at = [&](double roll) {
        const uint32_t m = next++;
        monster(b, m, 0, 15, 1);
        component<ecs::Boss>(b.r.fake, m);
        b.roll(roll);
        b.seconds(1);
        const uint32_t got = pick_of(b, m);
        b.r.fake.attrs[{m, EMF_ATTR_HEALTH}] = 0;
        b.seconds(1);
        return got;
    };
    CHECK(pick_at(0.64) == 1u);
    CHECK(pick_at(0.66) == 1234u);
}

TEST_CASE("threat: a monster that goes for another player or their beast is not taken into your fight") {
    RollRig b({{"call_cooldown_s", "0"}});
    write(b.data, "b 0x00000000000000bb wolf 0 1 Grey\n");  // player 2's wolf, out
    b.r.fake.session = 2;
    fire_load(1);
    next_beast(b, 1234);
    b.seconds(2);
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234).guardEntityId.id = 2;
    REQUIRE(has(b.reply("beastmaster.list", 2, 0xBB), "1. Grey - wolf, loyalty 1, out"));
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1235, 0, 4);
    b.roll(0.99);
    monster(b, 70, -6, 4, 1234);  // going for player 2's wolf
    monster(b, 71, -6, 5, 2);     // going for player 2
    monster(b, 72, -6, 6, 0);     // going for nothing
    for (uint32_t m : {70u, 71u, 72u}) deliver(b.r.fake, "HitEvent", hit(1235, m, -5));  // your boar bites them all
    one_step(b);
    const std::string held = b.reply("beastmaster.threat");
    CAPTURE(held);
    CHECK_FALSE(has(held, "70 on"));
    CHECK_FALSE(has(held, "71 on"));
    CHECK(has(held, "72 on Boar"));
    CHECK(pick_of(b, 70) != 1235u);
    CHECK(pick_of(b, 71) != 1235u);
}

TEST_CASE("threat: with no share for the beasts (both share settings 0) Beastmaster picks nothing") {
    RollRig b({{"tank_share_pct", "0"}, {"damage_share_pct", "0"}});
    b.reply("beastmaster.give boar");
    out_at(b, "Boar", 1234, 0, 4);
    b.roll(0.99);
    monster(b, 70, 6, 4, 1);
    b.seconds(2);
    CHECK(b.reply("beastmaster.threat") == "No monster is held.");
    CHECK(switches(b, 70));
}

// ---- the stable's E prompt (EMF API v25, Lua 1.14) ----
namespace {
size_t offers_on(BeastRig& b, uint32_t id) {
    size_t n = 0;
    for (const auto& o : b.r.fake.offers) n += o.entity == id;
    return n;
}
const Fake::Off* last_offer(BeastRig& b, uint32_t id) {
    for (auto it = b.r.fake.offers.rbegin(); it != b.r.fake.offers.rend(); ++it)
        if (it->entity == id) return &*it;
    return nullptr;
}
}  // namespace

TEST_CASE("stable: every stable gets the game's E Open prompt on its server entity, again at every search; one taken down loses it") {
    BeastRig b;
    with_offers(b.r.api);  // a v25 core
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 5, 10, 0);
    b.seconds(32);  // the next search
    REQUIRE(offers_on(b, 950) == 1u);
    const Fake::Off* o = last_offer(b, 950);
    CHECK(o->o.verb == EMF_VERB_OPEN);
    CHECK(o->text.empty());
    CHECK(o->world == kServer);
    CHECK(offers_on(b, 900) == 0u);
    b.seconds(30);
    CHECK(offers_on(b, 950) == 2u);  // again at the next search (the game may have dropped it)
    CHECK(b.r.fake.offers_removed.empty());
    remove_entity(b.r.fake, 950);  // taken down
    b.seconds(31);
    CHECK(contains(b.r.fake.offers_removed, 950));
}

TEST_CASE("stable: a bed only the host's client world shows gets its prompt on the server entity at its place") {
    BeastRig b;
    with_offers(b.r.api);
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 951, 10, 10, 0, nullptr);  // the bed on the server world: no render model there
    const auto model = guid_bytes(kStableModel);
    auto step_both = [&](int secs) {
        for (int i = 0; i < secs * 60; ++i) {
            place(b.r.fake, 950, 10, 10, 0);  // the client world's entity of the same bed, with its model
            b.r.fake.templates[{ecs::ModelResource::kHash, 950}] = model;
            add_entity(b.r.fake, 950);
            b.host->tick(kClient);
            remove_entity(b.r.fake, 950);
            b.r.fake.templates.erase({ecs::ModelResource::kHash, 950});
            b.r.fake.tick += 1;
            b.host->tick(kServer);
        }
    };
    step_both(40);
    REQUIRE(has(b.reply("beastmaster.stable list"), "1. Animal bed at 10, 10, 0"));
    CHECK(offers_on(b, 951) >= 1u);
    CHECK(offers_on(b, 950) == 0u);  // never the client world's id
}

TEST_CASE("stable: a marked prop gets its prompt at once, loses it when unmarked, and gets it back after a reload") {
    BeastRig b;
    with_offers(b.r.api);
    at_stable(b);  // prop 960 marked
    CHECK(offers_on(b, 960) == 1u);
    b.r.fake.session = 2;  // the world loads again: the mark is found by its place
    fire_load(1);
    b.seconds(1);
    CHECK(offers_on(b, 960) == 2u);
    REQUIRE(b.reply("beastmaster.stable unmark").rfind("Unmarked", 0) == 0);
    CHECK(contains(b.r.fake.offers_removed, 960));
}

namespace {
bool told(BeastRig& b, uint32_t player, const std::string& part) {
    for (const auto& [p, text] : b.r.fake.tells)
        if (p == player && text.find(part) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST_CASE("stable: E Open at a stable opens the Beasts window for the host and tells a player who joined the stable's commands") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);
    Fake::Off o = *last_offer(b, 960);
    o.cb(kServer, 1, 960, o.user);  // the host presses E there
    REQUIRE(b.r.fake.windows.size() == 1u);
    CHECK(b.r.fake.windows.back().id == "beastmaster.stable");
    CHECK(shows(b.r.fake.windows.back(), "Stable 3/8"));
    o.cb(kServer, 2, 960, o.user);  // a player who joined
    CHECK(b.r.fake.windows.size() == 1u);
    for (const char* part : {"/beastmaster active <name or number>", "/beastmaster store", "/beastmaster release",
                             "/beastmaster talent <name or number> <talent>"})
        CHECK_MESSAGE(told(b, 2, part), part);
    b.r.api.local_player = []() -> uint32_t { return 0; };  // a dedicated server: nobody has the window
    o.cb(kServer, 1, 960, o.user);
    CHECK(b.r.fake.windows.size() == 1u);
    CHECK(told(b, 1, "/beastmaster active"));
}

TEST_CASE("stable: the E key opens the Beasts window only at a stable without the prompt") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);
    REQUIRE(offers_on(b, 960) == 1u);
    component<ecs::InteractionOfferRuntime>(b.r.fake, 960);  // the game's prompt components, as the core adds them
    press(b, kE);
    CHECK(b.r.fake.windows.empty());  // the prompt opens it (once)
    BeastRig c;  // the game refused the prompt there (the prop has an E use of its own)
    with_offers(c.r.api);
    c.r.fake.offer_ok = false;
    three_beasts(c);
    at_stable(c);
    press(c, kE);
    CHECK(c.r.fake.windows.size() == 1u);
}

// ---- the stable's prompt: after review ----
TEST_CASE("stable: the E key opens the window at a stable whose prompt the game dropped (streamed out and back), and a new search starts") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);
    component<ecs::InteractionOfferRuntime>(b.r.fake, 960);
    REQUIRE(offers_on(b, 960) == 1u);
    b.r.fake.components.erase({ecs::InteractionOfferRuntime::kHash, 960});  // the game dropped the prompt
    press(b, kE);
    CHECK(b.r.fake.windows.size() == 1u);
    CHECK(offers_on(b, 960) == 2u);  // searched again at once: offered again
    BeastRig c;  // the prompt's id names something elsewhere now
    with_offers(c.r.api);
    three_beasts(c);
    at_stable(c);
    component<ecs::InteractionOfferRuntime>(c.r.fake, 960);
    place(c.r.fake, 960, 20, 10, 20);
    press(c, kE);
    CHECK(c.r.fake.windows.size() == 1u);
}

TEST_CASE("stable: the E key opens the window at a stable whose offer EMF dropped (the prompt's parts left on it), and it is offered again") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);
    component<ecs::InteractionOfferRuntime>(b.r.fake, 960);  // the game's prompt is on it
    REQUIRE(offers_on(b, 960) == 1u);
    press(b, kE);
    REQUIRE(b.r.fake.windows.empty());  // the prompt opens it, not the key
    b.r.fake.offer_active = false;  // EMF dropped the offer; its parts stay on the entity
    press(b, kE);
    CHECK(b.r.fake.windows.size() == 1u);
    CHECK(offers_on(b, 960) == 2u);  // searched again at once: offered again (EMF takes its dropped offer back)
}

TEST_CASE("stable: an id that names something elsewhere now is not offered again, and E Open from it does nothing") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);  // prop 960 marked and offered
    REQUIRE(offers_on(b, 960) == 1u);
    const Fake::Off old = *last_offer(b, 960);
    place(b.r.fake, 960, 20, 10, 20);  // the prop streamed out; the game gave its id to something else
    old.cb(kServer, 1, 960, old.user);  // E there before the stables are offered again
    CHECK(b.r.fake.windows.empty());
    stable_prop(b, 962, 3, 10, 0, nullptr);
    b.look(1, 0, 11.5, 0, 90);  // a second mark: the stables are offered again
    REQUIRE(b.reply("beastmaster.stable mark").rfind("Marked: ", 0) == 0);
    CHECK(offers_on(b, 962) == 1u);
    CHECK(offers_on(b, 960) == 1u);  // not again on the old id
}

TEST_CASE("stable: a stable whose entity has no StaticCollider gets no prompt; the E key opens its window") {
    BeastRig b;
    with_offers(b.r.api);
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 950, 5, 10, 0);  // a bed the server world shows, without a collider
    b.r.fake.components.erase({ecs::StaticCollider::kHash, 950});
    b.seconds(32);
    REQUIRE(has(b.reply("beastmaster.stable list"), "1. Animal bed at 5, 10, 0"));
    CHECK(offers_on(b, 950) == 0u);
    BeastRig c;  // a marked prop that has a model and no collider
    with_offers(c.r.api);
    three_beasts(c);
    stable_prop(c, 963, 0, 10, 3, kStableModel);
    c.r.fake.components.erase({ecs::StaticCollider::kHash, 963});
    c.look(1, 0, 11.5, 0, 0);
    REQUIRE(c.reply("beastmaster.stable mark").rfind("Marked: ", 0) == 0);
    CHECK(offers_on(c, 963) == 0u);
    press(c, kE);
    CHECK(c.r.fake.windows.size() == 1u);
}

TEST_CASE("stable: one E press that reaches both the prompt and the key opens the window once") {
    BeastRig b;
    with_offers(b.r.api);
    three_beasts(b);
    at_stable(b);  // offered; the game's prompt not seen there yet: the key path takes it as a stable without one
    Fake::Off o = *last_offer(b, 960);
    o.cb(kServer, 1, 960, o.user);  // the prompt opens the window
    REQUIRE(b.r.fake.windows.size() == 1u);
    press(b, kE);  // the same press, on the key path
    CHECK(b.r.fake.windows.size() == 1u);
}

TEST_CASE("stable: a window from before a world load that closes late does not close the one opened since") {
    BeastRig b;
    three_beasts(b);
    at_stable(b);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 1u);
    b.r.fake.session = 3;  // the world loads again
    fire_load(1);
    b.seconds(1);
    press(b, kE);
    REQUIRE(b.r.fake.windows.size() == 2u);
    const Fake::Win first = b.r.fake.windows[0];
    EmfUiEvent e{};
    e.size = sizeof e;
    e.kind = EMF_UI_CLOSED;
    e.window = first.id.c_str();
    e.key = "";
    e.from_key = "";
    e.reason = EMF_UI_BY_CORE;
    first.cb(kServer, &e, first.user);  // the first window's close arrives now
    click(b, "beast.Tusk");             // the open window still answers
    CHECK(text_at(shown(b), "tab.beast") == "Tusk");
}

// ---- idle: while their owner stands still the beasts pass the time; they follow the moment the owner moves ----
namespace {
constexpr uint32_t kSleepBit = 1u << 21;  // EnemyFeature Sleep (DynamicEnemy01BehaviorState.enabledFeatures)
constexpr uint32_t kRotate = 1u << 5;      // EnemyFeature Rotate
constexpr uint32_t kRotateBits = kRotate | (1u << 6) | (1u << 17);  // Rotate, RotateWhileStanding, IdleTurn
using AiState = emf::gen::enemy01::State;
const ecs::DynamicEnemy01BehaviorState& ai_now(BeastRig& b, uint32_t id) {
    return now_of<ecs::DynamicEnemy01BehaviorState>(b, id);
}
// Held by the game's sleep: the Sleep state, its feature bit on and an end time (its lease; a creature whose graph has
// sleep states lies down).
bool asleep(BeastRig& b, uint32_t id) {
    const auto& ai = ai_now(b, id);
    return ai.state == AiState::Sleep && (ai.enabledFeatures & kSleepBit) != 0 && ai.sleepEndTime.value > 0;
}
// Up and free: not in the Sleep state, the bit off, no end time.
bool awake(BeastRig& b, uint32_t id) {
    const auto& ai = ai_now(b, id);
    return ai.state != AiState::Sleep && (ai.enabledFeatures & kSleepBit) == 0 && ai.sleepEndTime.value == 0;
}
// Player 1's beast `name` (in the collection) called as `id` at (x, 10, z), guarding them, in the game's Roaming.
void roaming(BeastRig& b, const std::string& name, uint32_t id, double x, double z) {
    out_at(b, name, id, x, z);
    auto& ai = live_of<ecs::DynamicEnemy01BehaviorState>(b, id);
    ai.state = AiState::Roaming;
    ai.stateChangeTime.value = 1000000000;
}
}  // namespace

TEST_CASE("idle: a wolf out lies down after its owner stands still 10 s and gets up at once when the owner moves 2 m") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).disabledFeatures = kSleepBit | 1u;  // the game's own off-list
    b.seconds(9);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).state == AiState::Roaming);
    b.seconds(2);
    REQUIRE(asleep(b, 1234));
    CHECK(ai_now(b, 1234).disabledFeatures == 1u);  // not off while it sleeps
    place(b.r.fake, 1, 0.5, 10, 0);                 // a step: under 2 m from where idling began
    b.seconds(2);
    CHECK(asleep(b, 1234));
    place(b.r.fake, 1, 2.1, 10, 0);  // 2 m from it
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).state == AiState::Idle);
    CHECK(ai_now(b, 1234).disabledFeatures == (kSleepBit | 1u));  // as it was
    b.seconds(9);
    CHECK(awake(b, 1234));  // 10 s of stillness again
    b.seconds(2);
    CHECK(asleep(b, 1234));
}

TEST_CASE("idle: a hound lies down like a wolf; a sabretooth (no sleep states) with nothing to look at is left to roam, and roaming off past 15 m it drops out alone") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    b.reply("beastmaster.give hound");
    b.reply("beastmaster.give sabretooth");
    roaming(b, "Hound", 1234, 0, 4);
    roaming(b, "Sabretooth", 1235, 0, -4);
    b.seconds(11);
    CHECK(asleep(b, 1234));
    b.seconds(10);
    CHECK(awake(b, 1235));
    CHECK(ai_now(b, 1235).state == AiState::Roaming);
    place(b.r.fake, 1235, 0, 10, -17);  // it roams off to 17 m: nothing written, the hound sleeps on
    one_step(b);
    CHECK(asleep(b, 1234));
    CHECK(ai_now(b, 1235).state == AiState::Roaming);
}

TEST_CASE("idle: a bear lies down too (its pack's graph is a copy of the wolf's)") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give bear");
    roaming(b, "Bear", 1234, 0, 4);
    b.seconds(11);
    CHECK(asleep(b, 1234));
}

TEST_CASE("idle: each player's beasts follow only their own stillness") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    write(b.data, "b 0x00000000000000bb wolf 0 1 Grey\n");  // player 2's wolf, out
    b.r.fake.session = 2;
    fire_load(1);
    next_beast(b, 1234);
    b.seconds(1);
    REQUIRE(has(b.reply("beastmaster.list", 2, 0xBB), "1. Grey - wolf, loyalty 1, out"));
    auto& grey = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1234);
    grey.guardEntityId.id = 2;
    grey.state = AiState::Roaming;
    place(b.r.fake, 1234, 30, 10, 4);
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1235, 0, 4);
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    REQUIRE(asleep(b, 1235));
    place(b.r.fake, 2, 33, 10, 0);  // player 2 moves on
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(asleep(b, 1235));
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    remove_entity(b.r.fake, 2);  // player 2 leaves
    b.seconds(2);
    CHECK(contains(b.r.fake.destroyed, 1234));  // out of the world while away
    CHECK(asleep(b, 1235));
}

TEST_CASE("idle: a hit wakes the beasts at once; idling waits 10 s after the last hit to or from the owner or a beast, and while the threat share holds a monster for them") {
    RollRig b({{"idle_s", "5"}});
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    b.seconds(7);
    REQUIRE(asleep(b, 1234));
    creature(b, 70, 0, 20, kSkeletonFoe);
    deliver(b.r.fake, "HitEvent", hit(70, 1234, -5));  // a monster bites the wolf
    CHECK(awake(b, 1234));                             // in the event, before any step
    b.seconds(8);
    CHECK(awake(b, 1234));  // 10 s quiet first (idle_s is 5)
    b.seconds(3);
    REQUIRE(asleep(b, 1234));
    deliver(b.r.fake, "HitEvent", hit(1, 70, -5));  // the owner hits a monster
    CHECK(awake(b, 1234));
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    deliver(b.r.fake, "HitEvent", hit(0, 1, -5));        // a fall: no one's hit
    deliver(b.r.fake, "HitEvent", hit(1234, 1234, -5));  // its own
    CHECK(asleep(b, 1234));
    monster(b, 71, 10, 2, 1);  // a monster goes for the owner: the threat share holds it (on the owner, roll 0)
    b.seconds(2);
    REQUIRE(has(b.reply("beastmaster.threat"), "71 on you"));
    CHECK(awake(b, 1234));
    b.seconds(12);
    CHECK(awake(b, 1234));  // held still
    b.r.fake.attrs[{71, EMF_ATTR_HEALTH}] = 0;  // it falls: let go
    b.seconds(2);
    CHECK(asleep(b, 1234));
}

TEST_CASE("idle: Attack, dismiss and call wake every beast of that owner at once; idling starts anew after idle_s") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    roaming(b, "Wolf 2", 1235, 0, -4);
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    REQUIRE(asleep(b, 1235));
    b.reply("beastmaster.dismiss Wolf 2");
    CHECK(awake(b, 1234));
    b.seconds(9);
    CHECK(awake(b, 1234));
    b.seconds(2);
    REQUIRE(asleep(b, 1234));
    roaming(b, "Wolf 2", 1236, 0, -4);  // called again
    CHECK(awake(b, 1234));
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    REQUIRE(asleep(b, 1236));
    creature(b, 70, 0, 8, kSkeletonFoe);
    b.look(1, 0, 10.5, 0, 0);  // the owner looks at it
    REQUIRE(has(b.reply("beastmaster.attack"), "Your beasts attack (2)"));
    CHECK(awake(b, 1234));
    CHECK(awake(b, 1236));
    b.seconds(15);
    CHECK(awake(b, 1234));  // Attack's order holds 20 s
    CHECK(awake(b, 1236));
}

TEST_CASE("idle: building or the idle setting off end it; a beast not guarding its owner or farther than 15 m takes no part, and one that goes farther drops out alone") {
    RollRig b({{"call_cooldown_s", "0"}, {"make_room", "false"}});
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).guardEntityId.id = 0;  // not following its owner yet
    b.seconds(11);
    CHECK(awake(b, 1234));
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).guardEntityId.id = 1;
    b.seconds(1);
    REQUIRE(asleep(b, 1234));
    building(b.r.fake, 1, true);  // the owner holds something to build (make_room off: the beasts stay)
    one_step(b);
    CHECK(awake(b, 1234));
    b.seconds(15);
    CHECK(awake(b, 1234));
    building(b.r.fake, 1, false);
    b.r.fake.settings["beastmaster.idle"] = "false";
    b.seconds(15);
    CHECK(awake(b, 1234));
    b.r.fake.settings["beastmaster.idle"] = "1";
    b.seconds(1);
    REQUIRE(asleep(b, 1234));
    b.r.fake.settings["beastmaster.idle"] = "false";
    one_step(b);
    CHECK(awake(b, 1234));
    b.r.fake.settings["beastmaster.idle"] = "1";
    roaming(b, "Wolf 2", 1235, 0, -16);  // 16 m from the owner
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    CHECK(awake(b, 1235));
    CHECK(ai_now(b, 1235).state == AiState::Roaming);
    place(b.r.fake, 1235, 0, 10, -4);  // it comes within 15 m
    one_step(b);
    REQUIRE(asleep(b, 1235));
    place(b.r.fake, 1234, 0, 10, 16);  // the first wolf is 16 m away now: it alone gets up
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(asleep(b, 1235));
    place(b.r.fake, 1234, 0, 10, 5);  // back within 15 m: it takes part again
    one_step(b);
    CHECK(asleep(b, 1234));
}

namespace {
// A campfire or a fireplace at (x, 10, z): a ComfortProvider of category Hearth.
void fire_at(BeastRig& b, uint32_t id, double x, double z) {
    place(b.r.fake, id, x, 10, z);
    component<ecs::ComfortProvider>(b.r.fake, id).category = emf::gen::ecs::ComfortCategory::Hearth;
    add_entity(b.r.fake, id);
}
// A villager at (x, 10, z): the ActiveNpcState template half.
void villager_at(BeastRig& b, uint32_t id, double x, double z) {
    place(b.r.fake, id, x, 10, z);
    b.r.fake.templates[{ecs::ActiveNpcState::kHash, id}] = std::vector<uint8_t>(ecs::ActiveNpcState::kSize, 0);
    add_entity(b.r.fake, id);
}
// The game's teleports asked for entity `id` (a turn in place is one with a yaw).
std::vector<EmfTeleport> turns_of(BeastRig& b, uint32_t id) {
    std::vector<EmfTeleport> out;
    for (const auto& t : b.r.fake.teleports)
        if (t.entity == id) out.push_back(t);
    return out;
}
}  // namespace

TEST_CASE("idle: by the fire: a beast turns to face a fire within 15 m (a teleport in place with a yaw) and stays put; a wolf lies down facing it") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.6);  // by the fire
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.give wolf");
    fire_at(b, 900, 4, 4);        // a campfire 4 m east of the boar
    flame_altar(b, 901, 0, -8);   // a flame altar 4 m south of the wolf
    roaming(b, "Boar", 1234, 0, 4);
    roaming(b, "Wolf", 1235, 0, -4);
    b.seconds(11);
    const auto boar = turns_of(b, 1234);
    REQUIRE(boar.size() == 1u);
    CHECK(boar[0].x == doctest::Approx(0.0));  // where it stands
    CHECK(boar[0].y == doctest::Approx(10.0));
    CHECK(boar[0].z == doctest::Approx(4.0));
    CHECK((boar[0].flags & EMF_TELEPORT_FACE_YAW) != 0u);
    CHECK((boar[0].flags & EMF_TELEPORT_FIND_GROUND) == 0u);
    CHECK(boar[0].yaw_deg == doctest::Approx(90.0));  // +x
    CHECK(asleep(b, 1234));  // held in the Sleep state (a boar's graph has no sleep states: it stands there)
    const auto wolf = turns_of(b, 1235);
    REQUIRE(wolf.size() == 1u);
    CHECK(std::abs(wolf[0].yaw_deg) == doctest::Approx(180.0));  // -z
    CHECK(asleep(b, 1235));
}

TEST_CASE("idle: watching: a beast turns to a villager within 15 m and stays put; a sleeper lies down facing them") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.6);  // watching
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.give wolf");
    villager_at(b, 902, -3, 4);  // 3 m west of the boar
    roaming(b, "Boar", 1234, 0, 4);
    roaming(b, "Wolf", 1235, 0, -4);
    b.seconds(11);
    const auto boar = turns_of(b, 1234);
    REQUIRE(boar.size() == 1u);
    CHECK(boar[0].yaw_deg == doctest::Approx(-90.0));  // -x
    CHECK(asleep(b, 1234));
    const auto wolf = turns_of(b, 1235);
    REQUIRE(wolf.size() == 1u);
    CHECK(wolf[0].yaw_deg == doctest::Approx(std::atan2(-3.0, 8.0) * 180.0 / 3.14159265358979));
    CHECK(asleep(b, 1235));
}

TEST_CASE("idle: a fire or a villager farther than 15 m from the beast is not looked at") {
    RollRig b;
    b.roll(0.0);  // the first scene it can have
    b.reply("beastmaster.give boar");
    fire_at(b, 900, 0, 24);       // 20 m from the boar
    villager_at(b, 902, 20, 4);   // 20 m from it
    roaming(b, "Boar", 1234, 0, 4);
    b.seconds(11);
    CHECK(turns_of(b, 1234).empty());
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).state == AiState::Roaming);
}

TEST_CASE("idle: a new scene every 30 to 60 s; the same scene is kept without a second turn") {
    RollRig b;
    b.roll(0.0);  // settle; the next pick 30 s later
    b.reply("beastmaster.give wolf");
    fire_at(b, 900, 4, 4);
    roaming(b, "Wolf", 1234, 0, 4);
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    CHECK(turns_of(b, 1234).empty());
    b.roll(0.6);  // by the fire; the next pick 60 s later
    b.seconds(29);
    CHECK(turns_of(b, 1234).empty());
    b.seconds(1);
    REQUIRE(turns_of(b, 1234).size() == 1u);
    CHECK(turns_of(b, 1234)[0].yaw_deg == doctest::Approx(90.0));
    CHECK(asleep(b, 1234));
    b.seconds(60);  // by the fire again
    CHECK(turns_of(b, 1234).size() == 1u);
    CHECK(asleep(b, 1234));
    b.roll(0.99);  // sniffing about
    b.seconds(60);
    CHECK(awake(b, 1234));
}

TEST_CASE("idle: at night one beast keeps watch facing away from its owner while the sleepers settle; a lone wolf settles") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.99);  // by day this is sniffing about
    set_daytime(b.r.fake, 23.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    roaming(b, "Wolf", 1234, 0, 4);
    roaming(b, "Boar", 1235, 0, -4);
    b.seconds(11);
    CHECK(asleep(b, 1234));  // the wolf settles
    const auto watch = turns_of(b, 1235);
    REQUIRE(watch.size() == 1u);
    CHECK(std::abs(watch[0].yaw_deg) == doctest::Approx(180.0));  // away from its owner at (0, 10, 0)
    CHECK(asleep(b, 1235));                                      // a boar on watch stands still
    CHECK(turns_of(b, 1234).empty());
    set_daytime(b.r.fake, 12.0);
    b.seconds(60);  // the next picks are the day's
    CHECK(awake(b, 1234));
    CHECK(awake(b, 1235));
    b.reply("beastmaster.dismiss Boar");
    set_daytime(b.r.fake, 23.0);
    b.seconds(11);
    CHECK(asleep(b, 1234));  // alone it settles
}

TEST_CASE("idle: a turn the game refuses is skipped; the beast still stays put") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give boar");
    fire_at(b, 900, 4, 4);
    roaming(b, "Boar", 1234, 0, 4);
    b.r.fake.teleport_ok = false;  // e.g. a beast spawned a moment ago reads as dead
    b.seconds(11);
    CHECK(turns_of(b, 1234).size() == 1u);
    CHECK(asleep(b, 1234));
    CHECK(b.host->status("beastmaster") == "running");
}

TEST_CASE("idle: a beast that comes within 15 m of its owner while they idle gets a scene too") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    roaming(b, "Wolf 2", 1235, 0, -16);  // 16 m away: not in the idling
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    REQUIRE(awake(b, 1235));
    place(b.r.fake, 1235, 0, 10, -5);  // it comes back to its owner
    one_step(b);
    CHECK(asleep(b, 1235));
    CHECK(asleep(b, 1234));
}

TEST_CASE("idle: at the mod's start or a reload every player-side creature left with the Sleep bit is woken; a wild one is left alone") {
    BeastRig b;
    for (uint32_t id : {1300u, 1301u}) {
        creature(b, id, 5, 5, id == 1300u ? kPlayerAlly : kWildBeast);
        auto& ai = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, id);
        ai.enabledFeatures = kSleepBit | 4u;
        ai.state = AiState::Sleep;
        ai.sleepEndTime.value = 1000000000000000000LL;
    }
    b.edit_main("local wild_mod = require(\"wild\")\n", "-- reloaded\n");  // the script reloads; a second is played
    CHECK(awake(b, 1300));
    CHECK(ai_now(b, 1300).enabledFeatures == 4u);
    CHECK(ai_now(b, 1300).state == AiState::Idle);
    CHECK(asleep(b, 1301));
}

TEST_CASE("idle: an error in the idle code wakes every beast of the idling and is logged; the mod runs on") {
    RollRig b;
    b.edit_main("local wild_mod = require(\"wild\")\n", "emf.teleport = function() error(\"teleport broke\") end\n");
    b.roll(0.0);  // settle; the next pick 30 s later
    b.reply("beastmaster.give wolf");
    fire_at(b, 900, 4, 4);
    roaming(b, "Wolf", 1234, 0, 4);
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    b.roll(0.6);  // by the fire: the turn fails with an error
    b.seconds(30);
    CHECK(awake(b, 1234));
    auto logged = [&] {
        size_t n = 0;
        for (const auto& l : b.r.fake.log) n += has(l, "idle: ") && has(l, "teleport broke");
        return n;
    };
    CHECK(logged() == 1u);
    CHECK(b.host->status("beastmaster") == "running");
    b.seconds(11);  // idling again, by the fire again: the same error, not logged again
    CHECK(awake(b, 1234));
    CHECK(logged() == 1u);
}

TEST_CASE("idle: beastmaster.idle says what each beast does") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give boar");
    fire_at(b, 900, 4, -4);  // 4 m east of the boar
    roaming(b, "Wolf", 1234, 0, 4);
    roaming(b, "Boar", 1235, 0, -4);
    b.seconds(5);
    CHECK(has(b.reply("beastmaster.idle"), "Your beasts are not idling"));
    b.seconds(6);
    const std::string idle = b.reply("beastmaster.idle");
    CAPTURE(idle);
    CHECK(has(idle, "Wolf settles (lying down"));
    CHECK(has(idle, "Boar sits by the fire (standing still, turned"));
}

TEST_CASE("idle: at night with only wolves and hounds out none keeps watch: they all settle") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.99);  // by day this is sniffing about
    set_daytime(b.r.fake, 23.0);
    b.reply("beastmaster.give wolf");
    b.reply("beastmaster.give hound");
    roaming(b, "Wolf", 1234, 0, 4);
    roaming(b, "Hound", 1235, 0, -4);
    b.seconds(11);
    CHECK(asleep(b, 1234));
    CHECK(asleep(b, 1235));
    CHECK(turns_of(b, 1234).empty());
    CHECK(turns_of(b, 1235).empty());
}

TEST_CASE("idle: a turned beast keeps facing: the game's turning (Rotate, RotateWhileStanding, IdleTurn) is off while it is held, and back as it was when it gets up") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.0);
    b.reply("beastmaster.give boar");
    b.reply("beastmaster.give wolf");
    fire_at(b, 900, 4, 4);
    roaming(b, "Boar", 1234, 0, 4);
    roaming(b, "Wolf", 1235, 0, -4);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).disabledFeatures = kRotate | 1u;  // Rotate off already
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    REQUIRE(turns_of(b, 1234).size() == 1u);
    CHECK(ai_now(b, 1234).disabledFeatures == (kRotateBits | 1u));
    REQUIRE(asleep(b, 1235));  // settled, not turned: its turning is left alone
    CHECK(ai_now(b, 1235).disabledFeatures == 0u);
    place(b.r.fake, 1, 2.5, 10, 0);
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).disabledFeatures == (kRotate | 1u));  // only what was on before stays off
}

TEST_CASE("idle: the Sleep state is a lease: the game's time plus 120 s, renewed every 5 s while held; the game's own sleep check is pushed far ahead") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    const double called = b.r.fake.tick / 60.0;
    roaming(b, "Wolf", 1234, 0, 4);  // spawned at the game's time kSpawnNs
    auto lease = [&] { return kSpawnNs + static_cast<int64_t>((b.r.fake.tick / 60.0 - called) * 1e9) + 120000000000LL; };
    b.seconds(11);
    REQUIRE(asleep(b, 1234));
    CHECK(ai_now(b, 1234).sleepEndTime.value == lease());
    CHECK(ai_now(b, 1234).nextSleepCheckTime.value > kSpawnNs + 900000000000000000LL);
    const int64_t first = ai_now(b, 1234).sleepEndTime.value;
    b.seconds(4);
    CHECK(ai_now(b, 1234).sleepEndTime.value == first);
    b.seconds(1);
    CHECK(ai_now(b, 1234).sleepEndTime.value == lease());  // renewed
    CHECK(ai_now(b, 1234).sleepEndTime.value == first + 5000000000LL);
}

TEST_CASE("idle: without a call's spawn time the game's time is taken from the AI's own state change; until then nothing is held") {
    RollRig b;
    b.roll(0.0);
    b.look(1, 0, 10.5, 0, 0);
    b.beast(50, kWolfGraph, 0, 5, kWildBeast, 20, 100);
    REQUIRE(has(b.reply("beastmaster.tame"), "Taming"));
    b.seconds(4);
    REQUIRE(has(b.reply("beastmaster.list"), "1. Wolf - wolf, loyalty 1, out"));  // tamed, never called
    auto& ai = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 50);
    ai.guardEntityId.id = 1;
    ai.state = AiState::Roaming;
    ai.stateChangeTime.value = 1000000000;
    b.seconds(12);
    CHECK(awake(b, 50));  // idling, but the game's time is not known yet
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 50).stateChangeTime.value = 3000000000000LL;  // its state changes now
    one_step(b);
    CHECK(asleep(b, 50));
    CHECK(ai_now(b, 50).sleepEndTime.value == 3000000000000LL + 120000000000LL);
}

TEST_CASE("idle: a beast's Sleep bit is off once it gets up, also when the bit was on before") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    b.seconds(8);  // after the search at half of idle_s
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).enabledFeatures = kSleepBit | 2u;
    b.seconds(3);
    REQUIRE(asleep(b, 1234));
    place(b.r.fake, 1, 2.5, 10, 0);
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).enabledFeatures == 2u);
}

TEST_CASE("idle: a beast takes part only while its AI idles, roams or guards; one the game moves on (a fall, water) drops out alone") {
    RollRig b;
    b.roll(0.0);
    b.reply("beastmaster.give wolf");
    roaming(b, "Wolf", 1234, 0, 4);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).state = AiState::AvoidWater;
    b.seconds(11);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).state == AiState::AvoidWater);  // nothing written
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).state = AiState::Roaming;
    one_step(b);
    REQUIRE(asleep(b, 1234));
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).state = AiState::Falling;  // the game moves it on
    one_step(b);
    CHECK(awake(b, 1234));
    CHECK(ai_now(b, 1234).state == AiState::Falling);
    live_of<ecs::DynamicEnemy01BehaviorState>(b, 1234).state = AiState::Guard;
    one_step(b);
    CHECK(asleep(b, 1234));
}

TEST_CASE("idle: an error drops the search under way; the next waits 30 s") {
    RollRig b({{"idle_s", "5"}});
    b.edit_main("local wild_mod = require(\"wild\")\n", "emf.teleport = function() error(\"teleport broke\") end\n");
    b.roll(0.0);  // settle; the next pick 30 s later
    b.reply("beastmaster.give wolf");
    fire_at(b, 900, 4, 4);
    for (uint32_t id = 5000; id < 6000; ++id) add_entity(b.r.fake, id);  // a search takes 5 steps
    roaming(b, "Wolf", 1234, 0, 4);
    b.seconds(10);  // searched from 3 s of stillness on (5 steps); idling from 5 s
    REQUIRE(asleep(b, 1234));
    creature(b, 6100, 30, 30, kPlayerAlly);  // a player-side creature left asleep, the last entity
    auto& stray = component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 6100);
    stray.enabledFeatures = kSleepBit;
    stray.state = AiState::Sleep;
    stray.sleepEndTime.value = 1000000000000000000LL;
    b.seconds(25);  // 35 s: the next search has begun (30 s after the first)
    REQUIRE(asleep(b, 6100));
    b.roll(0.6);  // the pick at 36 s: by the fire, whose turn fails with an error
    b.seconds(1);
    REQUIRE(awake(b, 1234));
    b.roll(0.0);
    b.seconds(5);
    CHECK(asleep(b, 6100));  // the search under way was dropped
    b.seconds(30);
    CHECK(awake(b, 6100));  // the next one, 30 s after the error
}

// edit_main's reload check compares modification times, so two edits in a row must never get the same one: the system
// clock moves in steps of about 15 ms, and on a fast machine (CI) an edit made right after another fell in the same step,
// was not seen as a change, and the test ran on the old script.
TEST_CASE("rig: write_later gives every write a newer modification time, however fast they follow each other") {
    BeastRig b;
    const std::wstring path = b.folder + L"\\scratch.lua";
    auto stamp = [&] {
        WIN32_FILE_ATTRIBUTE_DATA d{};
        REQUIRE(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d));
        return (static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
    };
    write_later(path, "0", 1);
    uint64_t last = stamp();
    for (int i = 1; i < 300; ++i) {
        write_later(path, std::to_string(i), 1);
        const uint64_t now = stamp();
        REQUIRE(now > last);
        last = now;
    }
}

// ---- home: the beasts a player left at home live in the Large Stable near them ----
namespace {
constexpr double kPi = 3.14159265358979;
constexpr const char* kHomeBoar = "255fd8d9-4447-445d-8ffc-4d9dc0afdd16";
constexpr const char* kHomeHound = "87575915-bc75-4611-ab71-0cd4e79535b1";
constexpr const char* kHomeSabretooth = "50b96064-3ffc-47a2-8c7d-1d592a3c459c";
constexpr const char* kHomeAurochs = "d5f2b8e3-7c4a-4e9f-8b21-6a3c8d4e0f02";
// A Large Stable (the beasts pack's) `id` at (x, 10, z) in the base of the flame altar 900 at (0, 0), turned `yaw_deg`
// (0: its open side +z, 90: +x).
void large_stable(BeastRig& b, uint32_t id, double x, double z, double yaw_deg = 0) {
    flame_altar(b, 900, 0, 0);
    stable_prop(b, id, x, 10, z, kLargeOwnStableModel);
    auto& q = live_of<ecs::CurrentTransform>(b, id).transform.orientation;
    const double h = yaw_deg * kPi / 360.0;
    q.x = 0;
    q.y = static_cast<float>(std::sin(h));
    q.z = 0;
    q.w = static_cast<float>(std::cos(h));
}
// The creatures the next `n` spawns make: wild, level 1, 100 health, a SpawnTime (the game's clock), an AI state.
void homes_ready(BeastRig& b, int n) {
    const uint32_t first = b.r.fake.next_spawn_id;
    b.spawnable(n);
    for (int i = 0; i < n; ++i) {
        const uint32_t id = first + static_cast<uint32_t>(i);
        component<ecs::SpawnTime>(b.r.fake, id).spawnTime.value = kSpawnNs;
        component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, id);
    }
}
bool spawned_as(const Fake::Spawn& s, const char* guid) {
    const auto g = guid_bytes(guid);
    return std::memcmp(s.guid, g.data(), 16) == 0;
}
// A world point (x, z) in the frame of a stable at (sx, sz) turned yaw_deg: across it (first) and out of its open side.
std::pair<double, double> local_of(double x, double z, double sx, double sz, double yaw_deg) {
    const double a = yaw_deg * kPi / 180.0, dx = x - sx, dz = z - sz;
    return {dx * std::cos(a) - dz * std::sin(a), dx * std::sin(a) + dz * std::cos(a)};
}
// On the Large Stable's plank floor, between its walls (the Stable's inner floor times 1.6).
bool on_floor(const std::pair<double, double>& l) {
    return l.first > -2.34 && l.first < 2.49 && l.second > -1.44 && l.second < 2.27;
}
// The yaw of the last turn (a teleport with a yaw) asked for entity `id`, in [-180, 180); nan when none.
double last_yaw(BeastRig& b, uint32_t id) {
    double yaw = std::nan("");
    for (const auto& t : b.r.fake.teleports)
        if (t.entity == id && (t.flags & EMF_TELEPORT_FACE_YAW)) yaw = t.yaw_deg;
    return std::isnan(yaw) ? yaw : std::fmod(std::fmod(yaw + 180.0, 360.0) + 360.0, 360.0) - 180.0;
}
// Teleports with a ground search (a put-back, not a turn in place) asked for entity `id` to (x, z).
size_t moves_to(BeastRig& b, uint32_t id, double x, double z) {
    size_t n = 0;
    for (const auto& t : b.r.fake.teleports)
        n += t.entity == id && (t.flags & EMF_TELEPORT_FIND_GROUND) && std::abs(t.x - x) < 0.01 && std::abs(t.z - z) < 0.01;
    return n;
}
bool destroyed(BeastRig& b, uint32_t id) {
    return std::count(b.r.fake.destroyed.begin(), b.r.fake.destroyed.end(), id) > 0;
}
// A save of player 1's beasts (`lines`: "b ..." lines) in a world that has had a stable, loaded.
void home_save(BeastRig& b, const std::string& lines) {
    write(b.data, lines + "s 1\n");
    b.r.fake.session = 2;
    fire_load(1);
    b.seconds(1);
}
// Player 1's three beasts at home (three_beasts: Fang the wolf, Tusk the boar, Rex the hound) and a Large Stable at
// (10, 10, 0), found by the next search: they show up in it as 1234, 1235, 1236.
void home_three(BeastRig& b, double yaw_deg = 0) {
    three_beasts(b);
    large_stable(b, 950, 10, 0, yaw_deg);
    homes_ready(b, 3);
    b.seconds(32);
}
}  // namespace

TEST_CASE("home: the beasts a player left at home show up in the Large Stable near them, each on a spot of its own on "
          "its floor, as their allies keeping to the stable") {
    BeastRig b;
    home_three(b);
    REQUIRE(b.r.fake.spawns.size() == 3);
    CHECK(spawned_as(b.r.fake.spawns[0], kWolfTemplate));
    CHECK(spawned_as(b.r.fake.spawns[1], kHomeBoar));
    CHECK(spawned_as(b.r.fake.spawns[2], kHomeHound));
    const auto guards = b.guards();
    for (uint32_t i = 0; i < 3; ++i) {
        const auto& s = b.r.fake.spawns[i];
        CHECK(on_floor(local_of(s.x, s.z, 10, 0, 0)));
        CHECK(s.y > 10.0);
        CHECK(s.y < 11.0);
        CHECK(b.faction(1234 + i) == kPlayerAlly);
        CHECK(std::count(guards.begin(), guards.end(), std::make_pair(1234 + i, 950u)) >= 1);
        for (uint32_t j = 0; j < i; ++j)
            CHECK(std::hypot(s.x - b.r.fake.spawns[j].x, s.z - b.r.fake.spawns[j].z) >= 1.0);
    }
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 3);  // once
    CHECK(b.r.fake.destroyed.empty());
}

TEST_CASE("home: a Large Stable turned 90 degrees has its spots turned with it; the beasts turn to face out of its open "
          "side") {
    BeastRig b;
    home_three(b, 90);
    REQUIRE(b.r.fake.spawns.size() == 3);
    // the first spot, 1.75 m to the left and 0.45 m out, turned with the stable: 0.45 m along +x, 1.75 m along +z
    CHECK(b.r.fake.spawns[0].x == doctest::Approx(10.45));
    CHECK(b.r.fake.spawns[0].z == doctest::Approx(1.75));
    for (const auto& s : b.r.fake.spawns) CHECK(on_floor(local_of(s.x, s.z, 10, 0, 90)));
    b.seconds(2);
    for (uint32_t id : {1234u, 1235u, 1236u}) {
        const double yaw = last_yaw(b, id);
        REQUIRE_FALSE(std::isnan(yaw));
        CHECK(std::abs(yaw - 90) <= 20.5);
    }
}

TEST_CASE("home: a beast at home strolls first, then rests in the game's Sleep state (a wolf lies down, a boar stands "
          "still), then strolls again near the stable") {
    RollRig b;
    b.roll(0.0);  // the shortest scenes: a first stroll of 5 s, a rest of 40 s, a stroll of 15 s
    home_three(b);
    CHECK(awake(b, 1234));
    b.seconds(6);
    CHECK(asleep(b, 1234));
    CHECK(asleep(b, 1235));
    const size_t guarded = b.guards().size();
    b.seconds(40);
    CHECK(awake(b, 1234));
    CHECK(b.guards().size() > guarded);  // told to keep to the stable again
    b.seconds(15);
    CHECK(asleep(b, 1234));
}

TEST_CASE("home: a beast off the floor when its stroll ends is put back on its spot before it rests; one 6 m or more "
          "from the middle for 3 s at once") {
    RollRig b;
    b.roll(0.0);
    home_three(b);
    REQUIRE(b.r.fake.spawns.size() == 3);
    const Fake::Spawn spot = b.r.fake.spawns[0];
    place(b.r.fake, 1234, 10, 10, 4);  // it strolled out of the open side, 4 m from the middle
    b.seconds(6);
    REQUIRE(asleep(b, 1234));
    CHECK(moves_to(b, 1234, spot.x, spot.z) == 1);
    place(b.r.fake, 1234, spot.x, spot.y, spot.z);
    place(b.r.fake, 1235, 10, 10, 9);  // 9 m out
    const size_t before = moves_to(b, 1235, b.r.fake.spawns[1].x, b.r.fake.spawns[1].z);
    b.seconds(2);
    CHECK(moves_to(b, 1235, b.r.fake.spawns[1].x, b.r.fake.spawns[1].z) == before);
    b.seconds(3);
    CHECK(moves_to(b, 1235, b.r.fake.spawns[1].x, b.r.fake.spawns[1].z) == before + 1);
}

TEST_CASE("home: a beast at home never fights (its aggro and chosen targets are cleared every step) and is healed to "
          "full every second") {
    BeastRig b;
    home_three(b);
    creature(b, 70, 20, 10, kSkeletonFoe);
    fighting(b, 1234, 70);
    one_step(b);
    CHECK_FALSE(still_fighting(b, 1234));
    b.r.fake.attrs[{1235, EMF_ATTR_HEALTH}] = 40;
    b.seconds(1);
    CHECK(b.added(1235, EMF_ATTR_HEALTH, 60));
}

TEST_CASE("home: a beast called leaves the stable at once and comes beside its owner; sent home, it shows up in the "
          "stable again") {
    BeastRig b;
    home_three(b);
    next_beast(b, 1237);
    CHECK(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);
    CHECK(destroyed(b, 1234));
    REQUIRE(b.r.fake.spawns.size() == 4);
    CHECK(b.r.fake.spawns[3].x == doctest::Approx(2));  // beside its owner
    b.seconds(3);
    CHECK(b.r.fake.spawns.size() == 4);
    homes_ready(b, 1);
    b.reply("beastmaster.dismiss Fang");
    b.seconds(2);
    REQUIRE(b.r.fake.spawns.size() == 5);
    CHECK(spawned_as(b.r.fake.spawns[4], kWolfTemplate));
    CHECK(on_floor(local_of(b.r.fake.spawns[4].x, b.r.fake.spawns[4].z, 10, 0, 0)));
}

TEST_CASE("home: the beasts leave when their owner is more than 70 m from the stable, and show up again within 50 m") {
    BeastRig b;
    home_three(b);
    place(b.r.fake, 1, 70, 10, 0);  // 60 m: they stay
    b.seconds(2);
    CHECK(b.r.fake.destroyed.empty());
    place(b.r.fake, 1, 90, 10, 0);  // 80 m
    b.seconds(2);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
    homes_ready(b, 3);
    place(b.r.fake, 1, 65, 10, 0);  // 55 m: not yet
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 3);
    place(b.r.fake, 1, 40, 10, 0);  // 30 m
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 6);
}

TEST_CASE("home: a Large Stable has room for 4 beasts: with 6 at home, the first 4 in list order show") {
    BeastRig b;
    home_save(b, "b 0x00000000000000aa wolf 0 0 A\nb 0x00000000000000aa boar 0 0 B\nb 0x00000000000000aa hound 0 0 C\n"
                 "b 0x00000000000000aa sabretooth 0 0 D\nb 0x00000000000000aa wolf 0 0 E\nb 0x00000000000000aa boar 0 0 F\n");
    large_stable(b, 950, 10, 0);
    homes_ready(b, 6);
    b.seconds(32);
    REQUIRE(b.r.fake.spawns.size() == 4);
    CHECK(spawned_as(b.r.fake.spawns[3], kHomeSabretooth));
}

TEST_CASE("home: the Stable and the animal beds show no beasts; a Large Stable more than 50 m from the player shows none") {
    BeastRig b;
    three_beasts(b);
    flame_altar(b, 900, 0, 0);
    stable_prop(b, 951, 10, 10, 0, kOwnStableModel);
    stable_prop(b, 952, -10, 10, 0, kLargeStableModel);  // the large animal bed
    large_stable(b, 950, 60, 0);
    homes_ready(b, 3);
    b.seconds(32);
    CHECK(b.reply("beastmaster.stable list").find("3. ") != std::string::npos);  // all three are stables
    CHECK(b.r.fake.spawns.empty());
    place(b.r.fake, 1, 15, 10, 0);  // 45 m from the Large Stable
    b.seconds(2);
    CHECK(b.r.fake.spawns.size() == 3);
}

TEST_CASE("home: a beast at home that dies anyway is removed and shows up again 60 s later; its owner loses nothing") {
    BeastRig b;
    home_three(b);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 0;
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    b.seconds(2);
    CHECK(destroyed(b, 1234));  // its body
    homes_ready(b, 1);
    b.seconds(50);
    CHECK(b.r.fake.spawns.size() == 3);
    b.seconds(10);
    REQUIRE(b.r.fake.spawns.size() == 4);
    CHECK(spawned_as(b.r.fake.spawns[3], kWolfTemplate));
    next_beast(b, 1238);
    CHECK(b.reply("beastmaster.call Fang").find("comes") != std::string::npos);  // no rest
}

TEST_CASE("home: a player building within 20 m of the Large Stable sends its beasts away; they come back 5 s after the "
          "building stops") {
    BeastRig b;
    home_three(b);
    building(b.r.fake, 1, true);
    b.seconds(2);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
    homes_ready(b, 3);
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 3);
    building(b.r.fake, 1, false);
    b.seconds(3);
    CHECK(b.r.fake.spawns.size() == 3);
    b.seconds(4);
    CHECK(b.r.fake.spawns.size() == 6);
}

TEST_CASE("home: with the setting home_beasts off no beast shows; turned off while they show, they leave") {
    BeastRig off({{"home_beasts", "false"}});
    home_three(off);
    CHECK(off.r.fake.spawns.empty());
    BeastRig b;
    home_three(b);
    REQUIRE(b.r.fake.spawns.size() == 3);
    b.r.fake.settings["beastmaster.home_beasts"] = "false";
    b.seconds(2);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
    b.seconds(5);
    CHECK(b.r.fake.spawns.size() == 3);
}

TEST_CASE("home: a beast whose pack is not installed does not show (the others do) and is tried again after 5 minutes") {
    BeastRig b;
    home_save(b, "b 0x00000000000000aa aurochs 0 0 Horns\nb 0x00000000000000aa wolf 0 0 Fang\n");
    b.r.fake.missing_templates.insert(guid_bytes(kHomeAurochs));
    large_stable(b, 950, 10, 0);
    homes_ready(b, 1);
    b.seconds(32);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(spawned_as(b.r.fake.spawns[0], kWolfTemplate));
    b.r.fake.missing_templates.clear();
    homes_ready(b, 1);
    b.seconds(60);
    CHECK(b.r.fake.spawns.size() == 1);
    b.seconds(250);
    REQUIRE(b.r.fake.spawns.size() == 2);
    CHECK(spawned_as(b.r.fake.spawns[1], kHomeAurochs));
}

TEST_CASE("home: idle's search for player-side creatures left asleep leaves the beasts resting at home asleep") {
    RollRig b;
    b.roll(0.0);
    home_three(b);
    b.seconds(6);
    REQUIRE(asleep(b, 1236));
    out_at(b, "Fang", 1237, 0, 4);  // a beast out: its owner stands still, idle searches the world
    b.seconds(8);
    CHECK(asleep(b, 1236));
    CHECK(asleep(b, 1235));
}

TEST_CASE("home: after a reload of the script the beasts it had at home are taken out of the world before new ones "
          "show up; a player's beast out that stands in the stable stays") {
    BeastRig b;
    home_three(b);
    creature(b, 1300, 10.5, 0.5, kPlayerAlly);  // someone's beast out, on the floor: it guards its owner
    component<ecs::DynamicEnemy01BehaviorState>(b.r.fake, 1300).guardEntityId.id = 1;
    creature(b, 1301, 9.5, 0.5, kPlayerAlly);  // a player-side creature that is no beast (another graph): a villager
    for (uint32_t id : {1234u, 1235u, 1236u, 1300u}) {  // beasts: they run a beast's animation graph
        auto g = guid_bytes(kWolfGraph);
        g.resize(ecs::AnimationGraphResourceComponent2::kSize, 0);
        b.r.fake.templates[{ecs::AnimationGraphResourceComponent2::kHash, id}] = g;
    }
    homes_ready(b, 3);
    b.edit_main("local wild_mod = require(\"wild\")\n", "-- reloaded\n");
    b.seconds(33);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
    CHECK_FALSE(destroyed(b, 1300));
    CHECK_FALSE(destroyed(b, 1301));
    CHECK(b.r.fake.spawns.size() == 6);
}

TEST_CASE("home: an entity id the game gave to another creature is not ours any more: it is left alone, and the beast "
          "shows up again on a new one") {
    BeastRig b;
    home_three(b);
    b.seconds(2);                                                                 // its SpawnTime is known
    live_of<ecs::SpawnTime>(b, 1234).spawnTime.value = kSpawnNs + 5000000000LL;  // another creature on the id now
    homes_ready(b, 1);
    b.seconds(2);
    CHECK_FALSE(destroyed(b, 1234));
    REQUIRE(b.r.fake.spawns.size() == 4);
    CHECK(spawned_as(b.r.fake.spawns[3], kWolfTemplate));
}

TEST_CASE("home: a creature that does not become a player-side one is taken out at once, and its beast is not tried "
          "again for 5 minutes") {
    BeastRig b;
    home_save(b, "b 0x00000000000000aa wolf 0 0 Fang\n");
    large_stable(b, 950, 10, 0);
    homes_ready(b, 3);
    b.r.fake.components.erase({ecs::Faction::kHash, 1234});  // nothing to write its faction into
    b.seconds(32);
    REQUIRE(b.r.fake.spawns.size() == 1);
    CHECK(destroyed(b, 1234));
    b.seconds(10);
    CHECK(b.r.fake.spawns.size() == 1);
}

TEST_CASE("home: the game's time is taken again from each new beast at home, so its Sleep lease follows the game's "
          "clock") {
    RollRig b;
    b.roll(0.0);
    home_three(b);
    b.r.fake.attrs[{1234, EMF_ATTR_HEALTH}] = 0;
    deliver(b.r.fake, "EntityDiedEvent", died_event(1234));
    b.seconds(2);
    homes_ready(b, 1);  // 1237: the game's clock runs 1000 s ahead of the first beast's by now
    live_of<ecs::SpawnTime>(b, 1237).spawnTime.value = kSpawnNs + 1000000000000LL;
    b.seconds(60);
    REQUIRE(b.r.fake.spawns.size() == 4);
    b.seconds(8);
    REQUIRE(asleep(b, 1237));
    const int64_t ahead = ai_now(b, 1237).sleepEndTime.value - (kSpawnNs + 1000000000000LL);
    CHECK(ahead >= 120000000000LL);  // the lease: the game's time plus 120 s
    CHECK(ahead <= 140000000000LL);
}

TEST_CASE("home: a beast the game's Sleep state cannot hold strolls on, without being turned again every second") {
    RollRig b;
    b.roll(0.0);
    home_three(b);
    b.r.fake.components.erase({ecs::DynamicEnemy01BehaviorState::kHash, 1235});
    const size_t before = turns_of(b, 1235).size();
    b.seconds(12);
    CHECK(turns_of(b, 1235).size() <= before + 2);
}

TEST_CASE("home: with make_room off the beasts at home stay while someone builds near the stable") {
    BeastRig b({{"make_room", "false"}});
    home_three(b);
    building(b.r.fake, 1, true);
    b.seconds(3);
    CHECK(b.r.fake.destroyed.empty());
}

TEST_CASE("home: beastmaster.home says where your beasts at home are and what they do") {
    BeastRig b;
    three_beasts(b);
    CHECK(b.reply("beastmaster.home").find("None of your beasts is in a Large Stable now") != std::string::npos);
    large_stable(b, 950, 10, 0);
    homes_ready(b, 3);
    b.seconds(32);
    const std::string text = b.reply("beastmaster.home");
    CHECK(text.find("Fang: in the Large Stable at 10, 10, 0, strolling") != std::string::npos);
    CHECK(text.find("Rex: in the Large Stable at 10, 10, 0") != std::string::npos);
}

TEST_CASE("home: a beast at home has the health it has when called (its role, loyalty and talents)") {
    BeastRig b;
    three_beasts(b);
    large_stable(b, 950, 10, 0);
    homes_ready(b, 3);
    for (uint32_t id : {1234u, 1235u, 1236u}) component<ecs::Health>(b.r.fake, id).dataStorage[2] = 100;
    b.seconds(33);
    REQUIRE(b.r.fake.spawns.size() == 3);
    CHECK(now_of<ecs::Health>(b, 1234).dataStorage[3] == 20);  // the wolf's role: 1.2x
    CHECK(now_of<ecs::Health>(b, 1235).dataStorage[3] == 70);  // the boar's: 1.7x
}

TEST_CASE("home: a monster's hit on a beast at home sends that stable's beasts inside (out of the world); they come "
          "back 30 s after the last hit") {
    BeastRig b;
    home_three(b);
    creature(b, 70, 14, 4, kSkeletonFoe);
    deliver(b.r.fake, "HitEvent", hit_event(70, 1235));
    b.seconds(1);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
    homes_ready(b, 3);
    b.seconds(20);
    deliver(b.r.fake, "HitEvent", hit_event(70, 1));  // the monster hits the player at the stable: still inside
    b.seconds(25);
    CHECK(b.r.fake.spawns.size() == 3);
    b.seconds(7);
    CHECK(b.r.fake.spawns.size() == 6);
}

TEST_CASE("home: a monster's hit on a player at a Large Stable sends its beasts inside; a hit on a player far from it, "
          "or a fall, does not") {
    BeastRig b;
    home_three(b);
    creature(b, 70, 40, 40, kSkeletonFoe);
    place(b.r.fake, 2, 60, 10, 60);  // player 2, 78 m from the stable
    deliver(b.r.fake, "HitEvent", hit_event(70, 2));
    deliver(b.r.fake, "HitEvent", hit_event(0, 1));  // a fall: no source
    b.seconds(2);
    CHECK(b.r.fake.destroyed.empty());
    deliver(b.r.fake, "HitEvent", hit_event(70, 1));  // player 1, 10 m from the stable
    b.seconds(1);
    for (uint32_t id : {1234u, 1235u, 1236u}) CHECK(destroyed(b, id));
}

namespace {
// The game's last answer to teleport `t` (its callback): moved, or failed (given up after it was taken).
void answer(const EmfTeleport& t, bool moved) {
    EmfTeleportResult r{};
    r.size = sizeof r;
    r.entity = t.entity;
    r.status = moved ? EMF_TELEPORT_MOVED : EMF_TELEPORT_FAILED;
    r.flags = EMF_TELEPORT_LAST;
    r.reason = moved ? "" : "given up";
    if (t.cb) t.cb(kServer, &r, t.user);
}
}  // namespace

TEST_CASE("idle: a turn the game took but did not carry out (its teleport failed) is asked again, once") {
    RollRig b({{"call_cooldown_s", "0"}});
    b.roll(0.6);  // by the fire
    b.reply("beastmaster.give boar");
    fire_at(b, 900, 4, 4);
    roaming(b, "Boar", 1234, 0, 4);
    b.seconds(11);
    auto turns = turns_of(b, 1234);
    REQUIRE(turns.size() == 1u);
    answer(turns[0], false);
    one_step(b);
    turns = turns_of(b, 1234);
    REQUIRE(turns.size() == 2u);
    CHECK(turns[1].yaw_deg == doctest::Approx(90.0));
    answer(turns[1], false);  // failed again: not a third time
    b.seconds(2);
    CHECK(turns_of(b, 1234).size() == 2u);
}

TEST_CASE("home: a beast's turn to face out that the game did not carry out is asked again, at most 3 times") {
    RollRig b;
    b.roll(0.6);  // the longest first stroll (12 s): no turn of a rest meanwhile
    home_three(b);
    size_t answered = 0;
    for (int i = 0; i < 6; ++i) {
        const auto turns = turns_of(b, 1234);
        if (turns.size() > answered) answer(turns.back(), false);  // each turn answered once (the answer is its last)
        answered = turns.size();
        b.seconds(1);
    }
    CHECK(turns_of(b, 1234).size() == 3u);
}
