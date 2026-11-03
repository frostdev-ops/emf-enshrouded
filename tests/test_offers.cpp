// E prompts (EMF API v25, core/offers.h): the game's own interaction components a mod's offer is made of, the custom
// verb ids, and the registry that hands an accept to the mod that made the offer.
#include <doctest.h>

#include <cstring>
#include <map>
#include <set>
#include <string>

#include "core/component_service.h"
#include "core/offers.h"
#include "emf/generated/components.hpp"

using namespace emf::core;

namespace {
uint32_t u32_at(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}
}  // namespace

TEST_CASE("offers: the three components carry the verb, the sequence and the range at the game's offsets") {
    OfferSpec s;
    s.verb = 0x0C2E4515u;  // "Open"
    const auto ior = offer_runtime_bytes(s);
    REQUIRE(ior.size() == emf::gen::ecs::InteractionOfferRuntime::kSize);
    CHECK(u32_at(ior, offsetof(emf::gen::ecs::InteractionOfferRuntime, defaultSequence)) == kOfferDefaultSequence);
    CHECK(u32_at(ior, offsetof(emf::gen::ecs::InteractionOfferRuntime, defaultVerbId)) == 0x0C2E4515u);
    CHECK(u32_at(ior, offsetof(emf::gen::ecs::InteractionOfferRuntime, overrideSequence)) == 0u);
    CHECK(u32_at(ior, offsetof(emf::gen::ecs::InteractionOfferRuntime, overrideVerbLocaId)) == 0u);
    CHECK(ior[offsetof(emf::gen::ecs::InteractionOfferRuntime, isOffered)] == 0);  // the game sets it
    CHECK(ior[offsetof(emf::gen::ecs::InteractionOfferRuntime, reduceInteractionWeight)] == 0);
    CHECK(ior[offsetof(emf::gen::ecs::InteractionOfferRuntime, allowedAsBuildingSpark)] == 0);
    CHECK(offer_client_bytes() == std::vector<uint8_t>(emf::gen::ecs::ClientInteractionOffer::kSize, 0));
    CHECK(offer_range_bytes(s) == std::vector<uint8_t>{kOfferDefaultRange});

    s.sequence = 0xAF614FD8u;  // ButtonPress
    s.range_m = 12;
    s.flags = kOfferLowPriority | kOfferBuildMode;
    const auto b = offer_runtime_bytes(s);
    CHECK(u32_at(b, offsetof(emf::gen::ecs::InteractionOfferRuntime, defaultSequence)) == 0xAF614FD8u);
    CHECK(b[offsetof(emf::gen::ecs::InteractionOfferRuntime, reduceInteractionWeight)] == 1);
    CHECK(b[offsetof(emf::gen::ecs::InteractionOfferRuntime, allowedAsBuildingSpark)] == 1);
    CHECK(offer_range_bytes(s) == std::vector<uint8_t>{12});
}

TEST_CASE("offers: a custom text is a verb id of its own, the same on every PC, and wins over the verb") {
    const uint32_t a = custom_verb_id("Beasts");
    CHECK(a == custom_verb_id("Beasts"));
    CHECK(a != custom_verb_id("Open stable"));
    CHECK(a != 0u);
    // FNV-1a-32 of "emf.verb:Beasts"
    uint32_t h = 2166136261u;
    for (char c : std::string("emf.verb:Beasts")) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    CHECK(a == h);
    OfferSpec s;
    s.verb = 0x0C2E4515u;
    CHECK(offer_verb(s) == 0x0C2E4515u);
    s.text = "Beasts";
    CHECK(offer_verb(s) == a);
    const auto ior = offer_runtime_bytes(s);
    CHECK(u32_at(ior, offsetof(emf::gen::ecs::InteractionOfferRuntime, defaultVerbId)) == a);
    CHECK(offer_verb(OfferSpec{}) == kOfferDefaultVerb);  // neither: "Use"
}

TEST_CASE("offers: the registry hands an accept to the mod that offered, in that session only") {
    OfferRegistry r;
    int calls = 0;
    uint32_t got_player = 0, got_entity = 0;
    OfferSpec s;
    s.verb = 0x0C2E4515u;
    CHECK(r.add(7, 5000, "beastmaster", s, [&](EmfWorld*, uint32_t player, uint32_t entity) {
        ++calls;
        got_player = player;
        got_entity = entity;
    }));
    const auto cb = r.accept(7, 5000);
    REQUIRE(cb);
    (*cb)(nullptr, 1, 5000);
    CHECK(calls == 1);
    CHECK(got_player == 1);
    CHECK(got_entity == 5000);
    CHECK_FALSE(r.accept(7, 5001));  // another entity: not ours
    CHECK_FALSE(r.accept(8, 5000));  // another session: an entity with the same id is someone else's
    CHECK(r.owner(7, 5000) == "beastmaster");
}

TEST_CASE("offers: a second mod on the same entity is refused; the same mod replaces its own offer") {
    OfferRegistry r;
    OfferSpec a, b;
    a.verb = 1;
    b.verb = 2;
    CHECK(r.add(1, 42, "beastmaster", a, nullptr));
    CHECK_FALSE(r.add(1, 42, "other", b, nullptr));
    CHECK(r.find(1, 42)->spec.verb == 1);
    CHECK(r.add(1, 42, "beastmaster", b, nullptr));  // its own: the new offer
    CHECK(r.find(1, 42)->spec.verb == 2);
}

TEST_CASE("offers: remove, a new session and a mod's own clean-up leave nothing to call") {
    OfferRegistry r;
    OfferSpec s;
    CHECK(r.add(3, 10, "a", s, nullptr));
    CHECK(r.add(3, 11, "a", s, nullptr));
    CHECK(r.add(3, 12, "b", s, nullptr));
    CHECK_FALSE(r.remove(3, 10, "b"));  // only its owner removes it
    CHECK(r.remove(3, 10, "a"));
    CHECK_FALSE(r.accept(3, 10));
    CHECK(r.entities(3) == std::vector<uint32_t>{11, 12});
    CHECK(r.remove_owner("a") == std::vector<uint32_t>{11});
    CHECK(r.entities(3) == std::vector<uint32_t>{12});
    r.new_session(4);  // a world reload: every offer of the old world is gone with it
    CHECK(r.entities(3).empty());
    CHECK_FALSE(r.accept(3, 12));
    CHECK(r.size() == 0);
}

TEST_CASE("offers: accepts are counted per offer, for offers.list") {
    OfferRegistry r;
    CHECK(r.add(1, 9, "m", OfferSpec{}, nullptr));
    CHECK(r.accept(1, 9));
    CHECK(r.accept(1, 9));
    CHECK(r.find(1, 9)->accepts == 2);
}

// ---- OfferService over fake engine calls ----
namespace {
namespace ecs = emf::gen::ecs;

struct Fake {
    std::map<uint32_t, std::set<uint32_t>> comps;  // entity -> component hashes
    std::vector<ComponentChangeRequest> changes;
    std::vector<std::vector<OfferComponent>> spawned;
    uint32_t next_spawn = 7000, session = 1;
    std::vector<std::string> log;
    EmfWorld* world = reinterpret_cast<EmfWorld*>(uintptr_t(0x1000));

    OfferService::Deps deps() {
        OfferService::Deps d;
        d.change = [this](const ComponentChangeRequest& r, std::string*) {
            CHECK(r.internal);
            changes.push_back(r);
            return true;
        };
        d.spawn = [this](EmfWorld*, const uint8_t*, double, double, double, const std::vector<OfferComponent>& c,
                         std::string*) {
            spawned.push_back(c);
            const uint32_t id = next_spawn++;
            comps[id].insert(ecs::CurrentTransform::kHash);
            for (const auto& x : c) comps[id].insert(x.hash);
            return id;
        };
        d.has = [this](EmfWorld*, uint32_t hash, uint32_t entity) {
            auto e = comps.find(entity);
            return e != comps.end() && e->second.count(hash) != 0;
        };
        d.session = [this] { return session; };
        d.log = [this](const std::string& s) { log.push_back(s); };
        return d;
    }
    void prop(uint32_t id) { comps[id].insert(ecs::CurrentTransform::kHash); }
    // the component service's answers, as after a step: the changes land (or the given status)
    void answer_all(uint32_t status = EMF_COMPONENT_DONE) {
        auto pending = std::move(changes);
        changes.clear();
        for (auto& r : pending) {
            if (status == EMF_COMPONENT_DONE) {
                if (r.op == ComponentOp::Add) comps[r.entity].insert(r.hash);
                else comps[r.entity].erase(r.hash);
            }
            if (r.done) r.done(world, status, status == EMF_COMPONENT_DONE ? "" : "no");
        }
    }
    std::vector<uint8_t> accepted(uint32_t guest, uint32_t host) {
        std::vector<uint8_t> e(ecs::InteractionAcceptedEvent::kSize, 0);
        std::memcpy(e.data() + offsetof(ecs::InteractionAcceptedEvent, guestEntityId), &guest, 4);
        std::memcpy(e.data() + offsetof(ecs::InteractionAcceptedEvent, hostEntityId), &host, 4);
        return e;
    }
};

const uint8_t kStatue[16] = {0xd1, 0xa1, 0x3f, 0x50, 0xe3, 0xdc, 0xd1, 0x45, 0xa1, 0xc5, 0x18, 0x5d, 0x22, 0x66, 0x6d, 0x33};
}  // namespace

TEST_CASE("offer service: spawn_offer spawns the entity with the three components and hands accepts to the mod") {
    Fake f;
    OfferService s(f.deps());
    OfferSpec spec;
    spec.verb = 0x0C2E4515u;
    std::vector<std::pair<uint32_t, uint32_t>> got;
    const uint32_t id = s.spawn_offer(f.world, kStatue, 1, 2, 3, spec, "beastmaster",
                                      [&](EmfWorld*, uint32_t p, uint32_t e) { got.push_back({p, e}); }, nullptr);
    REQUIRE(id == 7000);
    REQUIRE(f.spawned.size() == 1);
    REQUIRE(f.spawned[0].size() == 3);
    CHECK(f.spawned[0][0].hash == ecs::InteractionOfferRuntime::kHash);
    CHECK(f.spawned[0][0].bytes == offer_runtime_bytes(spec));
    CHECK(f.spawned[0][1].hash == ecs::ClientInteractionOffer::kHash);
    CHECK(f.spawned[0][2].hash == ecs::CheckPlayerInRange::kHash);
    const auto ev = f.accepted(1, id);
    s.on_accept(f.world, ev.data(), static_cast<uint32_t>(ev.size()));
    const auto other = f.accepted(1, 4242);  // a vanilla chest: not EMF's
    s.on_accept(f.world, other.data(), static_cast<uint32_t>(other.size()));
    REQUIRE(got.size() == 1);
    CHECK(got[0] == std::make_pair(1u, id));
    CHECK(s.list().size() == 1);
}

TEST_CASE("offer service: an offer on an existing entity queues the three adds; a game offer, a player or none refuse") {
    Fake f;
    OfferService s(f.deps());
    f.prop(5000);
    std::string why;
    CHECK(s.offer(f.world, 5000, OfferSpec{}, "m", nullptr, &why));
    REQUIRE(f.changes.size() == 3);
    CHECK(f.changes[0].hash == ecs::InteractionOfferRuntime::kHash);
    CHECK(f.changes[0].op == ComponentOp::Add);
    CHECK(f.changes[1].hash == ecs::ClientInteractionOffer::kHash);
    CHECK(f.changes[2].hash == ecs::CheckPlayerInRange::kHash);
    f.answer_all();
    CHECK(f.comps[5000].count(ecs::InteractionOfferRuntime::kHash) == 1);

    f.prop(5001);
    f.comps[5001].insert(ecs::InteractionOfferRuntime::kHash);  // a chest: the game's own offer
    CHECK_FALSE(s.offer(f.world, 5001, OfferSpec{}, "m", nullptr, &why));
    CHECK(why.find("its own") != std::string::npos);
    f.prop(1);
    CHECK_FALSE(s.offer(f.world, 1, OfferSpec{}, "m", nullptr, &why));         // a player
    CHECK_FALSE(s.offer(f.world, 9999, OfferSpec{}, "m", nullptr, &why));      // no such entity
    CHECK_FALSE(s.offer(f.world, 5000, OfferSpec{}, "other", nullptr, &why));  // another mod's offer
    CHECK(f.changes.empty());

    f.prop(5002);
    f.comps[5002].insert(ecs::CheckPlayerInRange::kHash);  // already has a range check: only two adds
    CHECK(s.offer(f.world, 5002, OfferSpec{}, "m", nullptr, &why));
    CHECK(f.changes.size() == 2);
}

TEST_CASE("offer service: an offer whose add fails is dropped; no accept reaches the mod for it") {
    Fake f;
    OfferService s(f.deps());
    f.prop(5000);
    int calls = 0;
    CHECK(s.offer(f.world, 5000, OfferSpec{}, "m", [&](EmfWorld*, uint32_t, uint32_t) { ++calls; }, nullptr));
    f.answer_all(EMF_COMPONENT_FAILED);
    CHECK(s.list().empty());
    const auto ev = f.accepted(1, 5000);
    s.on_accept(f.world, ev.data(), static_cast<uint32_t>(ev.size()));
    CHECK(calls == 0);
}

TEST_CASE("offer service: remove takes the components off; only the owner may; a mod's clean-up removes all of its own") {
    Fake f;
    OfferService s(f.deps());
    f.prop(5000);
    f.prop(5001);
    CHECK(s.offer(f.world, 5000, OfferSpec{}, "m", nullptr, nullptr));
    CHECK(s.offer(f.world, 5001, OfferSpec{}, "m", nullptr, nullptr));
    f.answer_all();
    CHECK_FALSE(s.remove(f.world, 5000, "other", nullptr));
    CHECK(s.remove(f.world, 5000, "m", nullptr));
    REQUIRE(f.changes.size() == 3);  // IOR, CIO and the range check EMF added
    for (const auto& c : f.changes) CHECK(c.op == ComponentOp::Remove);
    f.answer_all();
    CHECK(f.comps[5000].count(ecs::InteractionOfferRuntime::kHash) == 0);
    CHECK(s.remove_owner(f.world, "m") == std::vector<uint32_t>{5001});
    CHECK(f.changes.size() == 3);
    CHECK(s.list().empty());
}

TEST_CASE("offer service: upkeep re-adds a lost offer, drops a gone entity and forgets an ended session") {
    Fake f;
    OfferService s(f.deps());
    f.prop(5000);
    f.prop(5001);
    CHECK(s.offer(f.world, 5000, OfferSpec{}, "m", nullptr, nullptr));
    CHECK(s.offer(f.world, 5001, OfferSpec{}, "m", nullptr, nullptr));
    f.answer_all();
    f.comps[5000].erase(ecs::InteractionOfferRuntime::kHash);  // streamed out and in: do_not_save parts are gone
    f.comps[5000].erase(ecs::ClientInteractionOffer::kHash);
    f.comps[5000].erase(ecs::CheckPlayerInRange::kHash);
    f.comps.erase(5001);  // destroyed
    for (int i = 0; i < kOfferUpkeepTicks; ++i) s.on_server_tick(f.world);
    CHECK(s.list().size() == 1);
    CHECK(f.changes.size() == 3);  // 5000's three again
    for (int i = 0; i < kOfferUpkeepTicks; ++i) s.on_server_tick(f.world);
    CHECK(f.changes.size() == 3);  // not asked twice while under way
    f.answer_all();
    f.session = 2;  // a new world
    for (int i = 0; i < kOfferUpkeepTicks; ++i) s.on_server_tick(f.world);
    CHECK(s.list().empty());
    CHECK(f.changes.empty());  // nothing pushed into the new world for the old one's ids
}
