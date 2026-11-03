// E prompts (EMF API v25, core/offers.h): the game's own interaction components a mod's offer is made of, the custom
// verb ids, and the registry that hands an accept to the mod that made the offer.
#include <doctest.h>

#include <cstring>
#include <string>

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
