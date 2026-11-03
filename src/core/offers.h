#pragma once
// E prompts (EMF API v25): the game's own "E <verb>" prompt on an entity a mod spawns or names, and the callback when a
// player accepts it (docs/research interaction_prompts, live on build 1285601). An offer is three dynamic components the
// game itself adds at run time (transform_to_loot, interaction_player_revive), all do_not_save:
//   InteractionOfferRuntime (server only): the verb (a loca tag id) and the action sequence the player plays;
//   ClientInteractionOffer: the replicated copy the prompt reads (the game fills it);
//   CheckPlayerInRange: keeps PlayerInRange on the entity while a player is within the range.
// The accept is InteractionAcceptedEvent {guest = player, host = entity} in the server world, for every player.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emf/emf_api.h"

namespace emf::core {

constexpr uint32_t kOfferDefaultSequence = 0x3E5189BFu;  // Interaction_TouchDevice: a short touch, no screen (live)
constexpr uint32_t kOfferDefaultVerb = 0x26437C13u;      // "Use"
constexpr uint8_t kOfferDefaultRange = 32;               // the value transform_to_loot uses
constexpr uint8_t kOfferLowPriority = 1;                 // reduceInteractionWeight: other targets win a tie
constexpr uint8_t kOfferBuildMode = 2;                   // allowedAsBuildingSpark: offered in build mode too

struct OfferSpec {
    uint32_t verb = 0;      // a loca tag id (the game's own verbs: every player sees the word); 0 = "Use"
    std::string text;       // custom words: a verb id of their own (custom_verb_id), shown on PCs with EMF
    uint32_t sequence = 0;  // the action sequence's hash; 0 = kOfferDefaultSequence
    uint8_t range_m = 0;    // 0 = kOfferDefaultRange
    uint8_t flags = 0;      // kOfferLowPriority, kOfferBuildMode
};

// FNV-1a-32 of "emf.verb:" + text: the same id on every PC for the same words.
uint32_t custom_verb_id(std::string_view text);
// The verb id an offer shows: its text's id, else its verb, else "Use".
uint32_t offer_verb(const OfferSpec& s);
// The components' bytes (InteractionOfferRuntime 56, ClientInteractionOffer 12 zeros, CheckPlayerInRange 1).
std::vector<uint8_t> offer_runtime_bytes(const OfferSpec& s);
std::vector<uint8_t> offer_client_bytes();
std::vector<uint8_t> offer_range_bytes(const OfferSpec& s);

using OfferCallback = std::function<void(EmfWorld* world, uint32_t player, uint32_t entity)>;

// The offers of the running server session, keyed by entity. One mod per entity; a session's offers go with it (the
// components are not saved, and a new world reuses entity ids).
class OfferRegistry {
public:
    struct Entry {
        uint32_t session = 0;
        uint32_t entity = 0;
        std::string owner;  // the mod (its module name, or the Lua script's id)
        OfferSpec spec;
        OfferCallback cb;
        uint32_t accepts = 0;
    };
    // False when another mod has an offer on the entity in that session (its own is replaced).
    bool add(uint32_t session, uint32_t entity, const std::string& owner, OfferSpec spec, OfferCallback cb);
    bool remove(uint32_t session, uint32_t entity, const std::string& owner);
    // The owner's callback for an accept of `host` in `session` (counted), or none.
    std::optional<OfferCallback> accept(uint32_t session, uint32_t host);
    const Entry* find(uint32_t session, uint32_t entity) const;
    std::string owner(uint32_t session, uint32_t entity) const;
    std::vector<uint32_t> entities(uint32_t session) const;  // ascending
    std::vector<uint32_t> remove_owner(const std::string& owner);  // a mod's clean-up: the entities it had
    void new_session(uint32_t session);  // drops every offer of other sessions
    size_t size() const { return entries_.size(); }
    std::vector<Entry> all() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

}  // namespace emf::core
