// Flame Blessings (mods/blessings): the commands, the per-player blessings, the screen line and the chat lines
// (blessings_logic.h, no game), then the game side (blessings_glue.h) over a fake API: refills, god mode through
// CombatFlags.immune and back, no fall damage, the screen line, the switch, commands, the wheel.
#include <doctest.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../mods/blessings/blessings_glue.h"
#include "../mods/blessings/blessings_logic.h"

using namespace blessings;

TEST_CASE("blessings: parse set with #k and name") {
    Command c = parse_command({"god", "on", "#2"});
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "god");
    CHECK(c.on);
    CHECK(c.target == "#2");

    c = parse_command({"Stamina", "OFF", "Ivar"});  // words ignore case; the name is kept as typed
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "stamina");
    CHECK_FALSE(c.on);
    CHECK(c.target == "Ivar");

    c = parse_command({"nofall", "on", "Big", "Ivar"});  // a name with a space
    CHECK(c.kind == Command::Set);
    CHECK(c.target == "Big Ivar");

    c = parse_command({"refill", "on"});  // no target: the sender
    CHECK(c.kind == Command::Set);
    CHECK(c.what == "refill");
    CHECK(c.target.empty());

    c = parse_command({"heal", "#1"});
    CHECK(c.kind == Command::Heal);
    CHECK(c.target == "#1");
    c = parse_command({"off"});
    CHECK(c.kind == Command::AllOff);
    CHECK(c.target.empty());
    CHECK(parse_command({}).kind == Command::Help);
    CHECK(parse_command({"help"}).kind == Command::Help);
    CHECK(parse_command({"list"}).kind == Command::List);
}

TEST_CASE("blessings: parse bad word gives usage") {
    for (const std::vector<std::string>& words :
         std::vector<std::vector<std::string>>{{"fly", "on"}, {"god"}, {"god", "maybe"}, {"list", "extra"}, {"mana", "1"}}) {
        CAPTURE(words.front());
        const Command c = parse_command(words);
        CHECK(c.kind == Command::Bad);
        CHECK(c.error.find("blessings <god|refill|stamina|mana|nofall> <on|off> [#k|name]") != std::string::npos);
    }
    CHECK(parse_command({"fly", "on"}).error.find("fly") != std::string::npos);  // says which word it did not know
}

TEST_CASE("blessings: god wins over refill; off clears both") {
    Blessings b = apply({}, "god", true);
    CHECK(b.health == Health::God);
    b = apply(b, "refill", true);  // a second admin asks for refill: god stays
    CHECK(b.health == Health::God);
    b = apply(b, "refill", false);  // and taking refill away leaves god mode alone
    CHECK(b.health == Health::God);
    CHECK(apply(b, "god", false).health == Health::Off);

    Blessings r = apply({}, "refill", true);
    CHECK(r.health == Health::Refill);
    CHECK(apply(r, "god", true).health == Health::God);
    CHECK(apply(r, "refill", false).health == Health::Off);
    CHECK(apply(r, "god", false).health == Health::Refill);  // god was not on: nothing to turn off

    b = apply(apply(apply(b, "stamina", true), "mana", true), "nofall", true);
    CHECK(b.stamina);
    CHECK(b.mana);
    CHECK(b.nofall);
    CHECK(b.any());
    CHECK_FALSE(apply(b, "mana", false).mana);
    CHECK_FALSE(Blessings{}.any());
    CHECK(apply({}, "nofall", true).any());
}

TEST_CASE("blessings: hud text order and refill wording") {
    CHECK(hud_text({}) == "");
    Blessings all;
    all.health = Health::God;
    all.stamina = all.mana = all.nofall = true;
    CHECK(hud_text(all) == "Blessings: god mode, stamina, mana, no fall damage");
    Blessings r;
    r.health = Health::Refill;
    r.nofall = true;
    CHECK(hud_text(r) == "Blessings: endless health, no fall damage");
    Blessings m;
    m.mana = true;
    CHECK(hud_text(m) == "Blessings: mana");
}

TEST_CASE("blessings: announce text") {
    CHECK(announce_text("Ember", "god", true, "Ivar") == "[Blessings] Ember: god mode on for Ivar");
    CHECK(announce_text("Host", "refill", false, "Ivar") == "[Blessings] Host: endless health off for Ivar");
    CHECK(announce_text("Host", "stamina", true, "Host") == "[Blessings] Host: endless stamina on for Host");
    CHECK(announce_text("Ember", "mana", true, "Ivar") == "[Blessings] Ember: endless mana on for Ivar");
    CHECK(announce_text("Ember", "nofall", true, "Ivar") == "[Blessings] Ember: no fall damage on for Ivar");
    CHECK(announce_text("Ember", "heal", true, "Ivar") == "[Blessings] Ember: full heal for Ivar");
    CHECK(announce_text("Ember", "off", false, "Ivar") == "[Blessings] Ember: all blessings off for Ivar");
}

TEST_CASE("blessings: prune drops players who left") {
    Roster r;
    set(r, 0xAA, apply({}, "god", true));
    set(r, 0xBB, apply({}, "stamina", true));
    prune(r, {0xBB, 0xCC});  // 0xAA left; 0xCC is new and has none
    CHECK(r.by_account.count(0xAA) == 0);
    CHECK(of(r, 0xBB).stamina);
    CHECK_FALSE(of(r, 0xCC).any());
    CHECK(r.by_account.count(0xCC) == 0);  // asking adds nobody
    set(r, 0xBB, {});                       // all off: no entry kept
    CHECK(r.by_account.empty());
}

TEST_CASE("blessings: a reused entity id does not inherit") {
    // Player 2 (account 0xAA) has god mode and leaves; a new player gets entity id 2 with account 0xBB. The roster is
    // keyed by account and each step looks the account of entity 2 up again: the newcomer has nothing.
    Roster r;
    set(r, 0xAA, apply({}, "god", true));
    const uint64_t account_of_entity_2 = 0xBB;
    prune(r, {account_of_entity_2});
    CHECK_FALSE(of(r, account_of_entity_2).any());
    CHECK(r.by_account.empty());
    // and the same player coming back later starts without blessings too
    prune(r, {0xAA});
    CHECK_FALSE(of(r, 0xAA).any());
}

TEST_CASE("blessings: #k is the k-th player, lowest id first; a name ignores case") {
    const std::vector<PlayerRef> players = {{5, "Ivar"}, {2, "Ember"}, {9, "big ivar"}};
    std::string err;
    CHECK(find_player("#1", players, &err) == 2u);
    CHECK(find_player("#2", players, &err) == 5u);
    CHECK(find_player("#3", players, &err) == 9u);
    CHECK(find_player("ember", players, &err) == 2u);
    CHECK(find_player("BIG IVAR", players, &err) == 9u);
    CHECK(find_player("#4", players, &err) == 0u);
    CHECK(err.find("#4") != std::string::npos);
    CHECK(find_player("#0", players, &err) == 0u);
    CHECK(find_player("Nobody", players, &err) == 0u);
    CHECK(err.find("Nobody") != std::string::npos);
    const std::vector<PlayerRef> twins = {{3, "Ivar"}, {4, "ivar"}};
    CHECK(find_player("Ivar", twins, &err) == 0u);  // two players with that name: ask for #k
    CHECK(err.find("#k") != std::string::npos);
}

TEST_CASE("blessings: the settings' words") {
    CHECK(health_of("god") == Health::God);
    CHECK(health_of("refill") == Health::Refill);
    CHECK(health_of("off") == Health::Off);
    CHECK(health_of("nonsense") == Health::Off);
    CHECK(std::string(health_value(Health::God)) == "god");
    CHECK(std::string(health_value(Health::Refill)) == "refill");
    CHECK(std::string(health_value(Health::Off)) == "off");
    CHECK(corner_of("top_right") == EMF_HUD_TOP_RIGHT);
    CHECK(corner_of("bottom_right") == EMF_HUD_BOTTOM_RIGHT);
    CHECK(corner_of("bottom_left") == EMF_HUD_BOTTOM_LEFT);
    CHECK(corner_of("") == EMF_HUD_BOTTOM_LEFT);
}

TEST_CASE("blessings: the wheel shows each switch with on or off, then Fill me up and All off") {
    Blessings b;
    b.health = Health::God;
    b.mana = true;
    CHECK(wheel_items(b) == std::vector<std::string>{"God mode (on)", "Endless health (off)", "Endless stamina (off)",
                                                     "Endless mana (on)", "No fall damage (off)", "Fill me up",
                                                     "All off"});
    // a pick toggles; Endless health while god mode is on switches to it (the wheel picks the health mode)
    CHECK(wheel_toggle(b, 0).health == Health::Off);
    CHECK(wheel_toggle(b, 1).health == Health::Refill);
    CHECK(wheel_toggle(wheel_toggle(b, 1), 1).health == Health::Off);
    CHECK(wheel_toggle(b, 2).stamina);
    CHECK_FALSE(wheel_toggle(b, 3).mana);
    CHECK(wheel_toggle(b, 4).nofall);
}

// ---- the game side over a fake API ----
namespace {
namespace gen = emf::gen::ecs;
EmfWorld* const kW = reinterpret_cast<EmfWorld*>(0x7000);

struct Fake {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> comps;
    std::map<uint32_t, std::map<uint32_t, int32_t>> attrs;  // entity -> attribute id -> value
    std::vector<std::pair<uint32_t, int32_t>> adds;          // (entity, delta) of every attribute_add
    bool attrs_on = true;                                    // AttrFind / AttrAdd resolved on this game build
    std::map<std::string, std::string> settings;             // blessings' settings as text
    std::vector<std::pair<std::string, std::string>> sets;   // setting_set calls
    std::vector<std::pair<std::string, uint32_t>> huds;      // hud_set (text, place) for blessings.hud
    std::vector<std::string> notes, log, broadcasts;
    std::map<uint32_t, std::string> names;
    std::vector<uint8_t> infos = std::vector<uint8_t>(0xA0 * 16, 0);  // BasicPlayerInfos: entity at +0, account at +8
    uint32_t local = 1;
    uint32_t session = 1;
};
Fake* f = nullptr;

void* f_get(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = f->comps.find({hash, entity});
    if (it == f->comps.end()) return nullptr;
    if (size) *size = it->second.size();
    return it->second.data();
}
uint8_t f_kind(EmfWorld*) { return EMF_WORLD_SERVER; }
uint32_t f_local() { return f->local; }
uint32_t f_session() { return f->session; }
void* f_singleton(EmfWorld*, const char* name) {
    return std::strcmp(name, "BasicPlayerInfos") == 0 ? f->infos.data() : nullptr;
}
void* f_resolve(const char* name) {
    static int marker;
    const bool attr = std::strcmp(name, "AttrFind") == 0 || std::strcmp(name, "AttrAdd") == 0;
    return attr && f->attrs_on ? &marker : nullptr;
}
bool f_attr_get(EmfWorld*, uint32_t e, uint32_t id, int32_t* out) {
    if (!f->attrs_on) return false;
    auto it = f->attrs.find(e);
    if (it == f->attrs.end() || !it->second.count(id)) return false;
    *out = it->second.at(id);
    return true;
}
bool f_attr_add(EmfWorld*, uint32_t e, uint32_t id, int32_t delta) {
    if (!f->attrs_on || !f->attrs.count(e) || !f->attrs[e].count(id)) return false;
    f->attrs[e][id] += delta;
    f->adds.push_back({e, delta});
    return true;
}
bool f_name(EmfWorld*, uint32_t player, char* buf, uint32_t size) {
    auto it = f->names.find(player);
    if (it == f->names.end()) return false;
    std::snprintf(buf, size, "%s", it->second.c_str());
    return true;
}
bool f_broadcast(const char* text) {
    f->broadcasts.push_back(text);
    return true;
}
bool f_hud(const char* id, const char* text, uint32_t place, uint32_t) {
    if (std::strcmp(id, kHudId) == 0) f->huds.push_back({text ? text : "", place});
    return true;
}
int64_t f_setting_int(const char*, const char* key, int64_t fallback) {
    auto it = f->settings.find(key);
    if (it == f->settings.end()) return fallback;
    return it->second == "1" || it->second == "true" ? 1 : std::atoll(it->second.c_str());
}
double f_setting_float(const char*, const char*, double fallback) { return fallback; }
uint32_t f_setting_text(const char*, const char* key, char* buf, uint32_t size) {
    auto it = f->settings.find(key);
    if (it == f->settings.end()) return 0;
    std::snprintf(buf, size, "%s", it->second.c_str());
    return static_cast<uint32_t>(it->second.size());
}
bool f_setting_set(const char*, const char* key, const char* value) {
    f->settings[key] = value;
    f->sets.push_back({key, value});
    return true;
}
void f_notify(const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    f->notes.push_back(b);
}
void f_log(const char*, const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    f->log.push_back(b);
}

template <class T>
T& plant(uint32_t entity) {
    auto& v = f->comps[{T::kHash, entity}];
    v.assign(sizeof(T), 0);
    return *reinterpret_cast<T*>(v.data());
}
template <class T>
T* comp(uint32_t entity) {
    auto it = f->comps.find({T::kHash, entity});
    return it == f->comps.end() ? nullptr : reinterpret_cast<T*>(it->second.data());
}
// A player entity standing at height y, with its account in BasicPlayerInfos, full health, stamina and mana.
void add_player(uint32_t e, uint64_t account, double y = 50.0) {
    plant<gen::PlayerState>(e);
    auto& ct = plant<gen::CurrentTransform>(e);
    ct.transform.position = {static_cast<int64_t>(100.0 * 4294967296.0), static_cast<int64_t>(y * 4294967296.0),
                             static_cast<int64_t>(-20.0 * 4294967296.0)};
    plant<gen::CombatFlags>(e).handleHits = true;
    std::memcpy(f->infos.data() + (e - 1) * 0xA0, &e, 4);
    std::memcpy(f->infos.data() + (e - 1) * 0xA0 + 8, &account, 8);
    f->attrs[e] = {{EMF_ATTR_HEALTH, 100}, {EMF_ATTR_MAX_HEALTH, 100}, {EMF_ATTR_STAMINA, 50},
                   {EMF_ATTR_MAX_STAMINA, 50}, {EMF_ATTR_MANA, 30},    {EMF_ATTR_MAX_MANA, 30}};
}
void remove_player(uint32_t e) {
    for (auto it = f->comps.begin(); it != f->comps.end();) it = it->first.second == e ? f->comps.erase(it) : std::next(it);
    std::memset(f->infos.data() + (e - 1) * 0xA0, 0, 0xA0);
    f->attrs.erase(e);
}
bool immune(uint32_t e) { return comp<gen::CombatFlags>(e)->immune; }
size_t count_with(const std::vector<std::string>& lines, const std::string& part) {
    return static_cast<size_t>(std::count_if(lines.begin(), lines.end(),
                                             [&](const std::string& l) { return l.find(part) != std::string::npos; }));
}

struct Rig {
    Fake fake;
    EmfApi api{};
    Glue g;
    emf::World w{kW};
    Rig() {
        f = &fake;
        api.version = EMF_API_VERSION;
        api.size = sizeof api;
        api.get_component = f_get;
        api.world_kind = f_kind;
        api.local_player = f_local;
        api.server_session = f_session;
        api.singleton = f_singleton;
        api.resolve = f_resolve;
        api.attribute_get = f_attr_get;
        api.attribute_add = f_attr_add;
        api.player_name = f_name;
        api.chat_broadcast = f_broadcast;
        api.hud_set = f_hud;
        api.setting_int = f_setting_int;
        api.setting_float = f_setting_float;
        api.setting_text = f_setting_text;
        api.setting_set = f_setting_set;
        api.notify = f_notify;
        api.log = f_log;
        REQUIRE(emf::init(&api));
        fake.settings = {{"health", "off"},  {"stamina", "0"}, {"mana", "0"},
                         {"no_fall_damage", "0"}, {"wheel_key", "F4"}, {"hud", "1"},
                         {"corner", "bottom_left"}, {"announce", "1"}};
        add_player(1, 0xAA);  // the host
    }
    ~Rig() {
        emf::api_ref() = nullptr;
        f = nullptr;
    }
    // A change in the Mods tab: the setting, then the core's change call.
    void setting(const std::string& key, const std::string& value) {
        fake.settings[key] = value;
        g.settings_changed();
    }
    std::string console(const std::vector<std::string>& words) { return g.command(w, words, Sender{}); }
    std::string chat(uint32_t from, const std::vector<std::string>& words) {
        Sender s;
        s.from_chat = true;
        s.entity = from;
        return g.command(w, words, s);
    }
};
}  // namespace

TEST_CASE("blessings tick: refill raises health to max") {
    Rig r;
    r.g.load();
    r.fake.attrs[1][EMF_ATTR_HEALTH] = 40;
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 10;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_HEALTH] == 40);  // no blessing yet

    r.setting("health", "refill");
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_HEALTH] == 100);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 10);  // stamina is its own blessing
    const size_t adds = r.fake.adds.size();
    r.g.tick(r.w);
    CHECK(r.fake.adds.size() == adds);  // full: nothing to add

    r.setting("stamina", "1");
    r.setting("mana", "1");
    r.fake.attrs[1][EMF_ATTR_MANA] = 2;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 50);
    CHECK(r.fake.attrs[1][EMF_ATTR_MANA] == 30);

    r.fake.attrs[1][EMF_ATTR_HEALTH] = 0;  // dead: endless health brings nobody back
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 0;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_HEALTH] == 0);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 0);
}

TEST_CASE("blessings tick: god sets immune and off restores it") {
    Rig r;
    r.g.load();
    r.setting("health", "god");
    r.fake.attrs[1][EMF_ATTR_HEALTH] = 70;
    r.g.tick(r.w);
    CHECK(immune(1));
    CHECK(r.fake.attrs[1][EMF_ATTR_HEALTH] == 100);  // god mode refills too
    comp<gen::CombatFlags>(1)->immune = false;        // the game wrote it back (a respawn?)
    r.g.tick(r.w);
    CHECK(immune(1));  // set again every step
    r.setting("health", "off");
    r.g.tick(r.w);
    CHECK_FALSE(immune(1));

    // a player the game itself had made immune keeps that when god mode ends
    comp<gen::CombatFlags>(1)->immune = true;
    r.setting("health", "god");
    r.g.tick(r.w);
    r.setting("health", "refill");
    r.g.tick(r.w);
    CHECK(immune(1));
}

TEST_CASE("blessings tick: nofall resets fallStartAltitude to the player's height") {
    Rig r;
    r.g.load();
    auto& fall = plant<gen::DynamicFallDamage>(1);
    fall.fallStartAltitude = 300.0f;  // jumped off a tower at 300 m
    r.g.tick(r.w);
    CHECK(comp<gen::DynamicFallDamage>(1)->fallStartAltitude == doctest::Approx(300.0f));
    r.setting("no_fall_damage", "1");
    r.g.tick(r.w);
    CHECK(comp<gen::DynamicFallDamage>(1)->fallStartAltitude == doctest::Approx(50.0f));  // where the player is
}

TEST_CASE("blessings tick: mod switched off restores immune and clears hud") {
    Rig r;
    r.g.load();
    r.setting("health", "god");
    r.setting("stamina", "1");
    r.g.tick(r.w);
    REQUIRE(immune(1));
    REQUIRE_FALSE(r.fake.huds.empty());
    CHECK(r.fake.huds.back().first == "Blessings: god mode, stamina");
    CHECK(r.fake.huds.back().second == EMF_HUD_BOTTOM_LEFT);

    r.g.set_switched_off(true);
    r.g.tick(r.w);
    CHECK_FALSE(immune(1));
    CHECK(r.fake.huds.back().first == "");
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 5;
    const size_t huds = r.fake.huds.size();
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 5);  // no blessing while switched off
    CHECK_FALSE(immune(1));
    CHECK(r.fake.huds.size() == huds);  // the line stays gone, nothing more to do
    CHECK(r.console({"god", "on"}).find("switched off") != std::string::npos);

    r.g.set_switched_off(false);  // switched on again: the host's settings apply at once
    r.g.tick(r.w);
    CHECK(immune(1));
    CHECK(r.fake.huds.back().first == "Blessings: god mode, stamina");
}

TEST_CASE("blessings tick: attribute feature off gives one 'not available' message, no repeat") {
    Rig r;
    r.fake.attrs_on = false;
    r.g.load();
    r.setting("stamina", "1");
    r.setting("health", "god");
    for (int i = 0; i < 5; ++i) r.g.tick(r.w);
    CHECK(count_with(r.fake.notes, "not available on this game version") == 1);
    CHECK(r.fake.adds.empty());
    CHECK(immune(1));  // god mode itself needs no attributes
    // a command says so too, and changes nothing
    const std::string reply = r.console({"mana", "on"});
    CHECK(reply.find("not available on this game version") != std::string::npos);
    CHECK(r.fake.settings["mana"] == "0");
    CHECK(r.console({"heal"}).find("not available on this game version") != std::string::npos);
    // switched off and on again (nothing needs them meanwhile): told once more
    r.setting("stamina", "0");
    r.g.tick(r.w);
    r.setting("stamina", "1");
    r.g.tick(r.w);
    r.g.tick(r.w);
    CHECK(count_with(r.fake.notes, "not available on this game version") == 2);
}

TEST_CASE("blessings tick: the screen line follows the host's settings and corner; none on a dedicated server") {
    Rig r;
    r.g.load();
    r.g.tick(r.w);
    CHECK(r.fake.huds.empty());  // nothing on, nothing shown
    r.setting("health", "refill");
    r.setting("no_fall_damage", "1");
    r.g.tick(r.w);
    REQUIRE(r.fake.huds.size() == 1);
    CHECK(r.fake.huds.back().first == "Blessings: endless health, no fall damage");
    r.g.tick(r.w);
    CHECK(r.fake.huds.size() == 1);  // only when it changes
    r.setting("corner", "top_right");
    r.g.tick(r.w);
    CHECK(r.fake.huds.back() == std::make_pair(std::string("Blessings: endless health, no fall damage"), EMF_HUD_TOP_RIGHT));
    r.setting("hud", "0");
    r.g.tick(r.w);
    CHECK(r.fake.huds.back().first == "");
}

TEST_CASE("blessings tick: on a dedicated server the Mods-tab switches act on nobody and show nothing") {
    Rig d;  // no local player: no host
    d.fake.local = 0;
    d.g.load();
    d.setting("health", "god");
    d.g.tick(d.w);
    CHECK(d.fake.huds.empty());
    CHECK_FALSE(immune(1));
}

TEST_CASE("blessings command: an admin blesses #2; the chat is told; the player leaving ends it") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{1, "Ember"}, {2, "Ivar"}};
    const std::string reply = r.console({"god", "on", "#2"});
    CHECK(reply.find("god mode on for Ivar") != std::string::npos);
    REQUIRE(r.fake.broadcasts.size() == 1);
    CHECK(r.fake.broadcasts[0] == "[Blessings] Host: god mode on for Ivar");
    r.g.tick(r.w);
    CHECK(immune(2));
    CHECK_FALSE(immune(1));  // the host is not touched
    CHECK(r.fake.settings["health"] == "off");
    CHECK(r.console({"list"}).find("Ivar (god mode)") != std::string::npos);

    // Ivar leaves and, before the next step, a new player gets entity 2 (the game had made them immune for a moment)
    remove_player(2);
    add_player(2, 0xCC);
    comp<gen::CombatFlags>(2)->immune = true;
    r.g.tick(r.w);
    CHECK(immune(2));  // never "restored" to Ivar's old value
    comp<gen::CombatFlags>(2)->immune = false;
    r.g.tick(r.w);
    CHECK_FALSE(immune(2));  // and no blessing of Ivar's
    // Ivar comes back: his blessings ended when he left
    remove_player(2);
    add_player(2, 0xBB);
    r.g.tick(r.w);
    CHECK_FALSE(immune(2));
}

TEST_CASE("blessings command: a chat admin names the sender; a name works; off clears both; no chat line when alone") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    add_player(3, 0xCC);
    r.fake.names = {{2, "Ivar"}, {3, "Ember"}};
    CHECK(r.chat(3, {"refill", "on", "ivar"}).find("endless health on for Ivar") != std::string::npos);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Ember: endless health on for Ivar");
    r.chat(3, {"god", "on", "Ivar"});  // a second admin: god wins
    r.g.tick(r.w);
    CHECK(immune(2));
    r.chat(3, {"refill", "off", "Ivar"});
    r.g.tick(r.w);
    CHECK(immune(2));  // god mode stays
    CHECK(r.chat(3, {"off", "Ivar"}).find("all blessings off for Ivar") != std::string::npos);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Ember: all blessings off for Ivar");
    r.g.tick(r.w);
    CHECK_FALSE(immune(2));
    r.fake.attrs[2][EMF_ATTR_HEALTH] = 10;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[2][EMF_ATTR_HEALTH] == 10);  // refill ended too

    // without a target: the chat sender
    r.chat(2, {"stamina", "on"});
    r.fake.attrs[2][EMF_ATTR_STAMINA] = 1;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[2][EMF_ATTR_STAMINA] == 50);
    CHECK(r.chat(2, {"god", "on", "Nobody"}).find("Nobody") != std::string::npos);
    CHECK(r.console({"fly", "on"}).find("usage") != std::string::npos);
    CHECK(r.console({}).find("give, put, time, goto, stack") != std::string::npos);

    // announce off: no chat line
    r.setting("announce", "0");
    r.g.tick(r.w);
    const size_t lines = r.fake.broadcasts.size();
    r.chat(3, {"mana", "on", "#2"});
    CHECK(r.fake.broadcasts.size() == lines);
}

TEST_CASE("blessings command: alone in the world there is nobody to tell in chat (the reply says it)") {
    Rig alone;
    alone.g.load();
    CHECK(alone.console({"god", "on"}).find("god mode on for Host") != std::string::npos);
    CHECK(alone.fake.broadcasts.empty());
}

TEST_CASE("blessings command: a blessing for the host is the host's setting, so the Mods tab follows") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    CHECK(r.console({"god", "on"}).find("god mode on for Host") != std::string::npos);
    CHECK(r.fake.settings["health"] == "god");
    r.console({"nofall", "on", "#1"});
    CHECK(r.fake.settings["no_fall_damage"] == "1");
    r.g.tick(r.w);
    CHECK(immune(1));
    CHECK(r.fake.huds.back().first == "Blessings: god mode, no fall damage");
    r.console({"off"});
    CHECK(r.fake.settings["health"] == "off");
    CHECK(r.fake.settings["no_fall_damage"] == "0");
    r.g.tick(r.w);
    CHECK_FALSE(immune(1));
    CHECK(r.fake.huds.back().first == "");
}

TEST_CASE("blessings command: heal fills health, stamina and mana once") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{2, "Ivar"}};
    r.fake.attrs[2][EMF_ATTR_HEALTH] = 3;
    r.fake.attrs[2][EMF_ATTR_STAMINA] = 4;
    r.fake.attrs[2][EMF_ATTR_MANA] = 5;
    CHECK(r.console({"heal", "#2"}).find("Ivar") != std::string::npos);
    CHECK(r.fake.attrs[2][EMF_ATTR_HEALTH] == 100);
    CHECK(r.fake.attrs[2][EMF_ATTR_STAMINA] == 50);
    CHECK(r.fake.attrs[2][EMF_ATTR_MANA] == 30);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Host: full heal for Ivar");
    r.fake.attrs[2][EMF_ATTR_HEALTH] = 3;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[2][EMF_ATTR_HEALTH] == 3);  // once, not a blessing
}

TEST_CASE("blessings: a new world session ends the admins' blessings; the host's stay") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.setting("stamina", "1");
    r.console({"god", "on", "#2"});
    r.g.tick(r.w);
    REQUIRE(immune(2));
    r.fake.session = 2;  // the world was reloaded: a fresh CombatFlags (not saved)
    comp<gen::CombatFlags>(2)->immune = false;
    r.g.tick(r.w);
    CHECK_FALSE(immune(2));
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 1;
    r.g.tick(r.w);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 50);
}

TEST_CASE("blessings wheel: a pick switches the host's blessing and the Mods tab follows") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{2, "Ivar"}};
    CHECK(r.g.wheel_items().front() == "God mode (off)");
    r.g.wheel_pick(r.w, 0);
    CHECK(r.fake.settings["health"] == "god");
    CHECK(r.fake.broadcasts.back() == "[Blessings] Host: god mode on for Host");
    CHECK(r.g.wheel_items().front() == "God mode (on)");
    r.g.wheel_pick(r.w, 1);  // Endless health instead
    CHECK(r.fake.settings["health"] == "refill");
    r.g.wheel_pick(r.w, 2);
    CHECK(r.fake.settings["stamina"] == "1");
    r.fake.attrs[1][EMF_ATTR_MANA] = 1;
    r.g.wheel_pick(r.w, 5);  // Fill me up
    CHECK(r.fake.attrs[1][EMF_ATTR_MANA] == 30);
    r.g.wheel_pick(r.w, 6);  // All off
    CHECK(r.fake.settings["health"] == "off");
    CHECK(r.fake.settings["stamina"] == "0");
    const size_t sets = r.fake.sets.size();
    r.g.wheel_pick(r.w, -1);  // cancelled
    CHECK(r.fake.sets.size() == sets);
}
