// Flame Blessings (mods/blessings): the commands, the per-player blessings, the screen line and the chat lines
// (blessings_logic.h, no game), then the game side (blessings_glue.h) over a fake API: refills, god mode through the
// Health minimum and back, no fall damage, the screen line, the switch, commands, the wheel.
#include <doctest.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../mods/blessings/blessings_glue.h"
#include "../mods/blessings/blessings_logic.h"
#include "core/overlay/console_suggest.h"

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
    CHECK(wheel_items(b) == std::vector<std::string>{"God mode (on)", "Endless health (off)", "Stamina (off)",
                                                     "Mana (on)", "No fall damage (off)", "Fill me up",
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
EmfWorld* const kW = reinterpret_cast<EmfWorld*>(0x7000);  // the server world
EmfWorld* const kC = reinterpret_cast<EmfWorld*>(0x8000);  // a client world (someone else's game, or the host's own)

struct Fake {
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>> comps;
    std::map<uint32_t, std::map<uint32_t, int32_t>> attrs;  // stamina and mana: entity -> attribute id -> value
    std::vector<std::pair<uint32_t, int32_t>> adds;          // (entity, delta) of every attribute_add
    bool attrs_on = true;                                    // AttrFind / AttrAdd resolved on this game build
    std::map<std::string, std::string> settings;             // blessings' settings as text
    std::vector<std::pair<std::string, std::string>> sets;   // setting_set calls
    std::set<std::string> refuse;                            // setting_set refuses these keys
    std::vector<std::pair<std::string, uint32_t>> huds;      // hud_set (text, place) for blessings.hud
    std::vector<std::string> notes, log, broadcasts;
    std::map<uint32_t, std::string> names;
    std::vector<uint8_t> infos = std::vector<uint8_t>(0xA0 * 16, 0);  // BasicPlayerInfos: entity at +0, account at +8
    uint32_t local = 1;
    uint32_t session = 1;
    uint64_t now_ms = 1000;
    bool changed = false;  // a setting_set since the last step
};
Fake* f = nullptr;

void* f_get(EmfWorld*, uint32_t hash, uint32_t entity, uint64_t* size) {
    auto it = f->comps.find({hash, entity});
    if (it == f->comps.end()) return nullptr;
    if (size) *size = it->second.size();
    return it->second.data();
}
uint8_t f_kind(EmfWorld* w) { return w == kC ? EMF_WORLD_CLIENT : EMF_WORLD_SERVER; }
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
gen::Health* health_of_entity(uint32_t e) {
    auto it = f->comps.find({gen::Health::kHash, e});
    return it == f->comps.end() ? nullptr : reinterpret_cast<gen::Health*>(it->second.data());
}
// Health through the game's attribute registry: slot 0 the current health, slot 7 the maximum (the player's layout).
// A change is held within [minimum (slot 1), maximum], as the game's attribute formulas do.
bool f_attr_get(EmfWorld*, uint32_t e, uint32_t id, int32_t* out) {
    if (!f->attrs_on) return false;
    if (id == EMF_ATTR_HEALTH || id == EMF_ATTR_MAX_HEALTH) {
        const gen::Health* h = health_of_entity(e);
        if (!h) return false;
        *out = h->dataStorage[id == EMF_ATTR_HEALTH ? 0 : 7];
        return true;
    }
    auto it = f->attrs.find(e);
    if (it == f->attrs.end() || !it->second.count(id)) return false;
    *out = it->second.at(id);
    return true;
}
bool f_attr_add(EmfWorld*, uint32_t e, uint32_t id, int32_t delta) {
    if (!f->attrs_on) return false;
    if (id == EMF_ATTR_HEALTH) {
        gen::Health* h = health_of_entity(e);
        if (!h) return false;
        h->dataStorage[0] = (std::max)(h->dataStorage[1], (std::min)(h->dataStorage[7], h->dataStorage[0] + delta));
        f->adds.push_back({e, delta});
        return true;
    }
    if (!f->attrs.count(e) || !f->attrs[e].count(id)) return false;
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
    if (f->refuse.count(key)) return false;
    f->settings[key] = value;
    f->sets.push_back({key, value});
    f->changed = true;  // the core calls the mod's change callback after the next step
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
void set_account(uint32_t e, uint64_t account) {
    std::memcpy(f->infos.data() + (e - 1) * 0xA0, &e, 4);
    std::memcpy(f->infos.data() + (e - 1) * 0xA0 + 8, &account, 8);
}
// A player entity standing at height y, with its account in BasicPlayerInfos (0: not known yet), full health
// (Health: 100 of 100, minimum 0, as the game lays it out for players), stamina and mana.
void add_player(uint32_t e, uint64_t account, double y = 50.0) {
    plant<gen::PlayerState>(e);
    auto& ct = plant<gen::CurrentTransform>(e);
    ct.transform.position = {static_cast<int64_t>(100.0 * 4294967296.0), static_cast<int64_t>(y * 4294967296.0),
                             static_cast<int64_t>(-20.0 * 4294967296.0)};
    auto& h = plant<gen::Health>(e);
    h.flags = 1;  // initialised
    h.storageOffset = 8;
    h.storageCount = 8;
    const int32_t storage[8] = {100, 0, 60, 40, 0, 0, 0, 100};
    std::memcpy(h.dataStorage, storage, sizeof storage);
    plant<gen::DynamicFallDamage>(e).fallStartAltitude = static_cast<float>(y);
    if (account) set_account(e, account);
    f->attrs[e] = {{EMF_ATTR_STAMINA, 50}, {EMF_ATTR_MAX_STAMINA, 50}, {EMF_ATTR_MANA, 30}, {EMF_ATTR_MAX_MANA, 30}};
}
void remove_player(uint32_t e) {
    for (auto it = f->comps.begin(); it != f->comps.end();) it = it->first.second == e ? f->comps.erase(it) : std::next(it);
    std::memset(f->infos.data() + (e - 1) * 0xA0, 0, 0xA0);
    f->attrs.erase(e);
}
int32_t& hp(uint32_t e) { return comp<gen::Health>(e)->dataStorage[0]; }
int32_t& min_hp(uint32_t e) { return comp<gen::Health>(e)->dataStorage[1]; }
int32_t& max_hp(uint32_t e) { return comp<gen::Health>(e)->dataStorage[7]; }
// A hit as the game applies it: attrAdd on the current health, held by the attribute formulas.
void hit(uint32_t e, int32_t damage) {
    gen::Health* h = comp<gen::Health>(e);
    h->dataStorage[0] = (std::max)(h->dataStorage[1], (std::min)(h->dataStorage[7], h->dataStorage[0] - damage));
}
size_t count_with(const std::vector<std::string>& lines, const std::string& part) {
    return static_cast<size_t>(std::count_if(lines.begin(), lines.end(),
                                             [&](const std::string& l) { return l.find(part) != std::string::npos; }));
}

struct Rig {
    Fake fake;
    EmfApi api{};
    Glue g;
    emf::World w{kW};
    emf::World client{kC};
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
        fake.settings = {{"health", "off"},       {"stamina", "0"},        {"mana", "0"},
                         {"no_fall_damage", "0"}, {"wheel_key", "F11"},    {"hud", "1"},
                         {"corner", "bottom_left"}, {"announce", "1"}};
        g.clock = [] { return f->now_ms; };
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
    void tick() {
        fake.now_ms += 16;
        if (fake.changed) {  // the core's change call for a setting_set, as after a step
            fake.changed = false;
            g.settings_changed();
        }
        g.tick(w);
    }
    std::string console(const std::vector<std::string>& words) { return g.command(w, words, Sender{}); }
    std::string chat(uint32_t from, const std::vector<std::string>& words, bool admin = true) {
        Sender s;
        s.from_chat = true;
        s.admin = admin;
        s.entity = from;
        return g.command(w, words, s);
    }
};
}  // namespace

TEST_CASE("blessings tick: refill raises health to max") {
    Rig r;
    r.g.load();
    hp(1) = 40;
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 10;
    r.tick();
    CHECK(hp(1) == 40);  // no blessing yet

    r.setting("health", "refill");
    r.tick();
    CHECK(hp(1) == 100);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 10);  // stamina is its own blessing
    CHECK(min_hp(1) == 0);                            // endless health leaves the minimum alone
    const size_t adds = r.fake.adds.size();
    r.tick();
    CHECK(r.fake.adds.size() == adds);  // full: nothing to add

    r.setting("stamina", "1");
    r.setting("mana", "1");
    r.fake.attrs[1][EMF_ATTR_MANA] = 2;
    r.tick();
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 50);
    CHECK(r.fake.attrs[1][EMF_ATTR_MANA] == 30);

    hp(1) = 0;  // dead: endless health brings nobody back
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 0;
    r.tick();
    CHECK(hp(1) == 0);
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 0);
}

TEST_CASE("blessings tick: god mode holds the minimum health at the maximum, so hits take nothing, and puts it back") {
    Rig r;
    r.g.load();
    r.setting("health", "god");
    hp(1) = 70;
    r.tick();
    CHECK(min_hp(1) == 100);
    CHECK(hp(1) == 100);  // and refills
    hit(1, 500);          // a blow far bigger than the health
    CHECK(hp(1) == 100);
    max_hp(1) = 120;  // a level up
    r.tick();
    CHECK(min_hp(1) == 120);
    CHECK(hp(1) == 120);
    r.setting("health", "off");
    r.tick();
    CHECK(min_hp(1) == 0);
    hit(1, 30);
    CHECK(hp(1) == 90);  // hits land again

    // a minimum the player had before god mode comes back as it was
    min_hp(1) = 5;
    r.setting("health", "god");
    r.tick();
    CHECK(min_hp(1) == 120);
    r.setting("health", "refill");
    r.tick();
    CHECK(min_hp(1) == 5);
}

TEST_CASE("blessings tick: god mode also stops fall damage") {
    Rig r;
    r.g.load();
    r.setting("health", "god");  // no_fall_damage stays off
    comp<gen::DynamicFallDamage>(1)->fallStartAltitude = 300.0f;
    r.tick();
    CHECK(comp<gen::DynamicFallDamage>(1)->fallStartAltitude == doctest::Approx(50.0f));
}

TEST_CASE("blessings tick: nofall lowers fallStartAltitude to the player's height, never raises it") {
    Rig r;
    r.g.load();
    auto* fall = comp<gen::DynamicFallDamage>(1);
    fall->fallStartAltitude = 300.0f;  // jumped off a tower at 300 m
    r.tick();
    CHECK(fall->fallStartAltitude == doctest::Approx(300.0f));
    r.setting("no_fall_damage", "1");
    r.tick();
    CHECK(fall->fallStartAltitude == doctest::Approx(50.0f));  // where the player is
    fall->fallStartAltitude = 20.0f;                           // below the player (climbing): left alone
    r.tick();
    CHECK(fall->fallStartAltitude == doctest::Approx(20.0f));
}

TEST_CASE("blessings tick: a missing Health or DynamicFallDamage is told once, only after it stayed missing a while") {
    Rig r;
    r.g.load();
    f->comps.erase({gen::DynamicFallDamage::kHash, 1u});
    comp<gen::Health>(1)->storageOffset = 12;  // not the layout EMF knows (a game update): god mode must not write
    r.setting("health", "god");
    r.setting("no_fall_damage", "1");
    for (int i = 0; i < kMissingTicks - 1; ++i) r.tick();
    CHECK(count_with(r.fake.log, "Health") == 0);  // a player who just joined may not be set up yet
    CHECK(count_with(r.fake.log, "DynamicFallDamage") == 0);
    for (int i = 0; i < 3 * kMissingTicks; ++i) r.tick();
    CHECK(count_with(r.fake.log, "Health") == 1);
    CHECK(count_with(r.fake.log, "DynamicFallDamage") == 1);
    CHECK(min_hp(1) == 0);  // never written
    CHECK(count_with(r.fake.notes, "god mode does not work") == 1);
}

TEST_CASE("blessings tick: mod switched off restores the minimum health and clears hud") {
    Rig r;
    r.g.load();
    r.setting("health", "god");
    r.setting("stamina", "1");
    r.tick();
    REQUIRE(min_hp(1) == 100);
    REQUIRE_FALSE(r.fake.huds.empty());
    CHECK(r.fake.huds.back().first == "Blessings: god mode, stamina");
    CHECK(r.fake.huds.back().second == EMF_HUD_BOTTOM_LEFT);

    r.g.set_switched_off(true);
    r.tick();
    CHECK(min_hp(1) == 0);
    CHECK(r.fake.huds.back().first == "");
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 5;
    const size_t huds = r.fake.huds.size();
    r.tick();
    CHECK(r.fake.attrs[1][EMF_ATTR_STAMINA] == 5);  // no blessing while switched off
    CHECK(min_hp(1) == 0);
    CHECK(r.fake.huds.size() == huds);  // the line stays gone, nothing more to do
    CHECK(r.console({"god", "on"}).find("switched off") != std::string::npos);

    r.g.set_switched_off(false);  // switched on again: the host's settings apply at once
    r.tick();
    CHECK(min_hp(1) == 100);
    CHECK(r.fake.huds.back().first == "Blessings: god mode, stamina");
}

TEST_CASE("blessings tick: attribute feature off gives one 'not available' message, no repeat") {
    Rig r;
    r.fake.attrs_on = false;
    r.g.load();
    r.setting("stamina", "1");
    r.setting("health", "god");
    for (int i = 0; i < 5; ++i) r.tick();
    CHECK(count_with(r.fake.notes, "not available on this game version") == 1);
    CHECK(r.fake.adds.empty());
    CHECK(min_hp(1) == 0);  // god mode checks the Health layout through those functions: nothing written blind
    // a command says so too, and changes nothing
    CHECK(r.console({"mana", "on"}).find("not available on this game version") != std::string::npos);
    CHECK(r.fake.settings["mana"] == "0");
    CHECK(r.console({"god", "on", "#1"}).find("not available on this game version") != std::string::npos);
    CHECK(r.console({"heal"}).find("not available on this game version") != std::string::npos);
    // switched off and on again (nothing needs them meanwhile): told once more
    r.setting("stamina", "0");
    r.setting("health", "off");
    r.tick();
    r.setting("stamina", "1");
    r.tick();
    r.tick();
    CHECK(count_with(r.fake.notes, "not available on this game version") == 2);
}

TEST_CASE("blessings tick: the screen line follows the host's settings and corner") {
    Rig r;
    r.g.load();
    r.tick();
    CHECK(r.fake.huds.empty());  // nothing on, nothing shown
    r.setting("health", "refill");
    r.setting("no_fall_damage", "1");
    r.tick();
    REQUIRE(r.fake.huds.size() == 1);
    CHECK(r.fake.huds.back().first == "Blessings: endless health, no fall damage");
    r.tick();
    CHECK(r.fake.huds.size() == 1);  // only when it changes
    r.setting("corner", "top_right");
    r.tick();
    CHECK(r.fake.huds.back() == std::make_pair(std::string("Blessings: endless health, no fall damage"), EMF_HUD_TOP_RIGHT));
    r.setting("hud", "0");
    r.tick();
    CHECK(r.fake.huds.back().first == "");
}

TEST_CASE("blessings tick: the screen line goes away in someone else's game and comes back with the server's ticks") {
    Rig r;
    r.g.load();
    r.setting("stamina", "1");
    r.tick();
    REQUIRE(r.fake.huds.back().first == "Blessings: stamina");
    // the host's own game: the client world ticks between the server's
    r.fake.now_ms += 500;
    r.g.tick(r.client);
    CHECK(r.fake.huds.back().first == "Blessings: stamina");
    // someone else's game: only client ticks, no server world for 2 s
    r.fake.now_ms += 2000;
    r.g.tick(r.client);
    CHECK(r.fake.huds.back().first == "");
    const size_t huds = r.fake.huds.size();
    r.fake.now_ms += 100;
    r.g.tick(r.client);
    CHECK(r.fake.huds.size() == huds);  // cleared once
    r.tick();                           // a world of one's own again
    CHECK(r.fake.huds.back().first == "Blessings: stamina");
}

TEST_CASE("blessings tick: on a dedicated server the Mods-tab switches act on nobody and show nothing") {
    Rig d;  // no local player: no host
    d.fake.local = 0;
    d.g.load();
    d.setting("health", "god");
    d.tick();
    CHECK(d.fake.huds.empty());
    CHECK(min_hp(1) == 0);
}

TEST_CASE("blessings tick: the host keeps their blessings through a moment without a local player") {
    Rig r;
    r.g.load();
    r.setting("health", "god");
    r.tick();
    REQUIRE(min_hp(1) == 100);
    r.fake.local = 0;  // the game says "no local player" for a step
    r.tick();
    CHECK(min_hp(1) == 100);
    hp(1) = 60;
    r.tick();
    CHECK(hp(1) == 100);
    CHECK(r.fake.huds.back().first == "Blessings: god mode");
    r.fake.local = 1;
    r.setting("health", "off");
    r.tick();
    CHECK(min_hp(1) == 0);
}

TEST_CASE("blessings tick: the minimum kept before god mode belongs to the entity, also when its account shows up late") {
    Rig r;
    std::memset(r.fake.infos.data(), 0, 0xA0);  // the host's account is not known on the first steps
    r.g.load();
    r.tick();
    min_hp(1) = 3;
    r.setting("health", "god");
    r.tick();
    REQUIRE(min_hp(1) == 100);
    set_account(1, 0xAA);  // now it is
    r.tick();
    CHECK(min_hp(1) == 100);
    r.setting("health", "off");
    r.tick();
    CHECK(min_hp(1) == 3);  // the value from before god mode, never the god mode's own
}

TEST_CASE("blessings tick: a minimum health left from before (a crash?) is set back to 0 once, with a log line") {
    Rig r;
    add_player(2, 0xBB);
    min_hp(2) = 100;
    r.g.load();
    r.tick();  // nothing blessed: still looked at
    r.fake.now_ms += 2000;
    for (int i = 0; i < kScanTicks; ++i) r.tick();
    CHECK(min_hp(2) == 0);
    CHECK(count_with(r.fake.log, "minimum health") == 1);
    min_hp(2) = 7;  // the game's own doing later: left alone
    for (int i = 0; i < 2 * kScanTicks; ++i) r.tick();
    CHECK(min_hp(2) == 7);
    CHECK(count_with(r.fake.log, "minimum health") == 1);
}

TEST_CASE("blessings command: an admin blesses #2; the chat is told; the player leaving ends it") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{1, "Ember"}, {2, "Ivar"}};
    r.tick();
    min_hp(2) = 3;  // Ivar's own minimum (after EMF first looked at him)
    const std::string reply = r.console({"god", "on", "#2"});
    CHECK(reply.find("god mode on for Ivar") != std::string::npos);
    REQUIRE(r.fake.broadcasts.size() == 1);
    CHECK(r.fake.broadcasts[0] == "[Blessings] Host: god mode on for Ivar");
    r.tick();
    CHECK(min_hp(2) == 100);
    CHECK(min_hp(1) == 0);  // the host is not touched
    CHECK(count_with(r.fake.log, "minimum health") == 0);
    CHECK(r.fake.settings["health"] == "off");
    const std::string list = r.console({"list"});
    CHECK(list.find("#1 Ember: none") != std::string::npos);
    CHECK(list.find("#2 Ivar: god mode") != std::string::npos);
    CHECK(list.find("#1") < list.find("#2"));

    // Ivar leaves and, before the next step, a new player gets entity 2
    remove_player(2);
    add_player(2, 0xCC);
    r.tick();
    CHECK(min_hp(2) == 0);  // never "restored" to Ivar's old minimum
    hit(2, 30);
    r.tick();
    CHECK(hp(2) == 70);  // and no blessing of Ivar's
    // Ivar comes back: his blessings ended when he left
    remove_player(2);
    add_player(2, 0xBB);
    r.tick();
    CHECK(min_hp(2) == 0);
    CHECK(r.console({"list"}).find("#2 Ivar: none") != std::string::npos);
}

TEST_CASE("blessings command: a chat admin names the sender; a name works; off clears both; announce off is quiet") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    add_player(3, 0xCC);
    r.fake.names = {{2, "Ivar"}, {3, "Ember"}};
    CHECK(r.chat(3, {"refill", "on", "ivar"}).find("endless health on for Ivar") != std::string::npos);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Ember: endless health on for Ivar");
    r.chat(3, {"god", "on", "Ivar"});  // a second admin: god wins
    r.tick();
    CHECK(min_hp(2) == 100);
    r.chat(3, {"refill", "off", "Ivar"});
    r.tick();
    CHECK(min_hp(2) == 100);  // god mode stays
    CHECK(r.chat(3, {"off", "Ivar"}).find("all blessings off for Ivar") != std::string::npos);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Ember: all blessings off for Ivar");
    r.tick();
    CHECK(min_hp(2) == 0);
    hp(2) = 10;
    r.tick();
    CHECK(hp(2) == 10);  // refill ended too

    // without a target: the chat sender
    r.chat(2, {"stamina", "on"});
    r.fake.attrs[2][EMF_ATTR_STAMINA] = 1;
    r.tick();
    CHECK(r.fake.attrs[2][EMF_ATTR_STAMINA] == 50);
    CHECK(r.chat(2, {"god", "on", "Nobody"}).find("Nobody") != std::string::npos);
    CHECK(r.console({"fly", "on"}).find("usage") != std::string::npos);
    CHECK(r.console({}).find("give, put, time, goto, stack") != std::string::npos);

    // announce off: no chat line
    r.setting("announce", "0");
    r.tick();
    const size_t lines = r.fake.broadcasts.size();
    r.chat(3, {"mana", "on", "#2"});
    CHECK(r.fake.broadcasts.size() == lines);
}

TEST_CASE("blessings command: a player who is no admin is refused, whatever reaches the mod") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    CHECK(r.chat(2, {"god", "on"}, false).find("for the host and admins") != std::string::npos);
    r.tick();
    CHECK(min_hp(2) == 0);
    CHECK(r.fake.broadcasts.empty());
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
    r.tick();
    CHECK(min_hp(1) == 100);
    CHECK(r.fake.huds.back().first == "Blessings: god mode, no fall damage");
    r.console({"off"});
    CHECK(r.fake.settings["health"] == "off");
    CHECK(r.fake.settings["no_fall_damage"] == "0");
    r.tick();
    CHECK(min_hp(1) == 0);
    CHECK(r.fake.huds.back().first == "");
    CHECK(r.fake.broadcasts.size() == 3);  // god on, nofall on, all off: each told once, not again by the settings
}

TEST_CASE("blessings command: a host setting the core refuses to write is said, and the blessing stays as it was") {
    Rig r;
    r.g.load();
    r.fake.refuse = {"health"};
    const std::string reply = r.console({"god", "on"});
    CHECK(reply.find("could not") != std::string::npos);
    r.tick();
    CHECK(min_hp(1) == 0);
    CHECK(r.fake.huds.empty());
}

TEST_CASE("blessings: the host's Mods-tab changes are told in chat once, with the host as the sender") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{2, "Ivar"}};
    r.setting("health", "god");
    r.tick();
    REQUIRE(r.fake.broadcasts.size() == 1);
    CHECK(r.fake.broadcasts[0] == "[Blessings] Host: god mode on for Host");
    r.setting("health", "refill");
    r.setting("mana", "1");
    r.tick();
    REQUIRE(r.fake.broadcasts.size() == 3);
    CHECK(r.fake.broadcasts[1] == "[Blessings] Host: endless health on for Host");
    CHECK(r.fake.broadcasts[2] == "[Blessings] Host: endless mana on for Host");
    r.setting("health", "off");
    r.tick();
    CHECK(r.fake.broadcasts.back() == "[Blessings] Host: endless health off for Host");
    r.setting("corner", "top_right");  // not a blessing: nothing to tell
    r.tick();
    CHECK(r.fake.broadcasts.size() == 4);
    r.g.wheel_pick(r.w, 4);  // a wheel pick: told once (its own line), not again when the setting comes back
    r.tick();
    r.tick();
    CHECK(r.fake.broadcasts.size() == 5);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Host: no fall damage on for Host");
}

TEST_CASE("blessings command: heal fills health, stamina and mana once, and says when there was nothing to fill") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.fake.names = {{2, "Ivar"}};
    hp(2) = 3;
    r.fake.attrs[2][EMF_ATTR_STAMINA] = 4;
    r.fake.attrs[2][EMF_ATTR_MANA] = 5;
    CHECK(r.console({"heal", "#2"}).find("Ivar is filled up") != std::string::npos);
    CHECK(hp(2) == 100);
    CHECK(r.fake.attrs[2][EMF_ATTR_STAMINA] == 50);
    CHECK(r.fake.attrs[2][EMF_ATTR_MANA] == 30);
    CHECK(r.fake.broadcasts.back() == "[Blessings] Host: full heal for Ivar");
    hp(2) = 3;
    r.tick();
    CHECK(hp(2) == 3);  // once, not a blessing
    hp(2) = 100;
    const size_t lines = r.fake.broadcasts.size();
    CHECK(r.console({"heal", "#2"}).find("already full") != std::string::npos);
    hp(2) = 0;
    CHECK(r.console({"heal", "#2"}).find("dead") != std::string::npos);
    CHECK(hp(2) == 0);
    f->comps.erase({gen::Health::kHash, 2u});
    CHECK(r.console({"heal", "#2"}).find("could not read") != std::string::npos);
    CHECK(r.fake.broadcasts.size() == lines);  // nothing filled: nothing told
}

TEST_CASE("blessings: a new world session ends the admins' blessings; the host's stay") {
    Rig r;
    r.g.load();
    add_player(2, 0xBB);
    r.setting("stamina", "1");
    r.console({"god", "on", "#2"});
    r.tick();
    REQUIRE(min_hp(2) == 100);
    r.fake.session = 2;  // the world was reloaded: a fresh Health (not saved)
    min_hp(2) = 0;
    r.tick();
    CHECK(min_hp(2) == 0);
    r.fake.attrs[1][EMF_ATTR_STAMINA] = 1;
    r.tick();
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

TEST_CASE("blessings: the command's help offers every first word to the console's suggestions") {
    // the help as blessings.cpp registers it (gen_llms_txt.py reads it the same way), through the console's own parser
    std::ifstream in(EMF_SOURCE_DIR "/mods/blessings/blessings.cpp", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::smatch m;
    const std::string src = ss.str();
    REQUIRE(std::regex_search(src, m, std::regex(R"re(add_command\(\s*"blessings",\s*"([^"]*)")re")));
    const auto hint = emf::core::parse_hint(emf::core::split_help(m[1].str()).args);
    REQUIRE_FALSE(hint.empty());
    for (const char* word : {"god", "refill", "stamina", "mana", "nofall", "heal", "off", "list", "help"}) {
        CAPTURE(word);
        CHECK(std::find(hint[0].choices.begin(), hint[0].choices.end(), word) != hint[0].choices.end());
    }
}
