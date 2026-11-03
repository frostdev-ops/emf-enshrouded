#pragma once
// E prompts (EMF API v25): the game's own "E <verb>" prompt on an entity a mod spawns or names, and the callback when a
// player accepts it (docs/research interaction_prompts, live on build 1285601). An offer is three dynamic components the
// game itself adds at run time (transform_to_loot, interaction_player_revive), all do_not_save:
//   InteractionOfferRuntime (server only): the verb (a loca tag id) and the action sequence the player plays;
//   ClientInteractionOffer: the replicated copy the prompt reads (the game fills it);
//   CheckPlayerInRange: keeps PlayerInRange on the entity while a player is within the range.
// The accept is InteractionAcceptedEvent {guest = player, host = entity} in the server world, for every player.
#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "component_service.h"
#include "emf/emf_api.h"

namespace emf::core {

constexpr uint32_t kOfferDefaultSequence = 0x3E5189BFu;  // Interaction_TouchDevice: a short touch, no screen (live)
constexpr uint32_t kOfferDefaultVerb = 0x26437C13u;      // "Use"
constexpr uint8_t kOfferDefaultRange = 32;               // the value transform_to_loot uses
constexpr uint8_t kOfferLowPriority = 1;                 // reduceInteractionWeight: other targets win a tie
constexpr uint8_t kOfferBuildMode = 2;                   // allowedAsBuildingSpark: offered in build mode too

struct OfferSpec {
    uint32_t verb = 0;      // a loca tag id (the game's own verbs: every player sees the word); 0 = "Use"
    std::string text;       // custom words: a verb id of their own (custom_verb_id); the prompt shows no word yet
    uint32_t sequence = 0;  // the action sequence's hash; 0 = kOfferDefaultSequence
    uint8_t range_m = 0;    // 0 = kOfferDefaultRange
    uint8_t flags = 0;      // kOfferLowPriority, kOfferBuildMode
};

// FNV-1a-32 of "emf.verb:" + text: the same id on every PC for the same words.
uint32_t custom_verb_id(std::string_view text);
// The verb id an offer shows: its text's id, else its verb, else "Use".
uint32_t offer_verb(const OfferSpec& s);
// Custom words registered by offers (any thread): the id is custom_verb_id(text). Kept for a later way to show them.
void register_verb_text(const std::string& text);
std::optional<std::string> verb_text(uint32_t id);
size_t verb_text_count();
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

constexpr int kOfferUpkeepTicks = 60;  // the upkeep runs about once a second (60 server steps)

struct OfferComponent {
    uint32_t hash = 0;
    std::vector<uint8_t> bytes;
};

// The offers in the running server session: spawned with their components, or added to an entity through the
// component service (internal requests, three records as the game pushes them), handed to the owner on an accept, kept
// through streaming (the upkeep adds a lost offer again) and forgotten with the session. The API layer checks the world
// (the server world, inside a world call); everything here runs on the server world's thread.
class OfferService {
public:
    struct Deps {
        std::function<bool(const ComponentChangeRequest&, std::string*)> change;  // ComponentService::request
        std::function<uint32_t(EmfWorld*, const uint8_t guid[16], double, double, double, const std::vector<OfferComponent>&,
                               std::string*)>
            spawn;
        std::function<bool(EmfWorld*, uint32_t hash, uint32_t entity)> has;  // the entity has that component
        // the (defaultSequence, defaultVerbId) of the entity's InteractionOfferRuntime; nullopt when it has none
        std::function<std::optional<std::pair<uint32_t, uint32_t>>(EmfWorld*, uint32_t entity)> read_offer;
        std::function<uint32_t()> session;
        std::function<void(const std::string&)> log;
    };
    explicit OfferService(Deps deps);
    OfferService(const OfferService&) = delete;
    OfferService& operator=(const OfferService&) = delete;

    uint32_t spawn_offer(EmfWorld* w, const uint8_t guid[16], double x, double y, double z, const OfferSpec& spec,
                         const std::string& owner, OfferCallback cb, std::string* why);
    bool offer(EmfWorld* w, uint32_t entity, const OfferSpec& spec, const std::string& owner, OfferCallback cb,
               std::string* why);
    bool remove(EmfWorld* w, uint32_t entity, const std::string& owner, std::string* why);
    // A mod's clean-up: every offer it has goes; returns the entities.
    std::vector<uint32_t> remove_owner(EmfWorld* w, const std::string& owner);
    // The entity was destroyed (through EMF): its offer is forgotten at once (its id may soon be another entity's).
    void forget(uint32_t entity);
    // InteractionAcceptedEvent in the server world.
    void on_accept(EmfWorld* w, const void* event, uint32_t size);
    // After every server step.
    void on_server_tick(EmfWorld* w);
    std::vector<OfferRegistry::Entry> list() const;

private:
    struct State {
        int pending = 0;            // changes under way
        bool range_added = false;   // EMF added CheckPlayerInRange (the entity had none)
        bool removing = false;      // taken off: no accepts; the removes go when the adds landed
        bool removes_sent = false;  // ... and are under way
    };
    using Key = std::pair<uint32_t, uint32_t>;  // session, entity
    // The adds an entity still lacks (what it has stays), as internal requests.
    std::vector<ComponentChangeRequest> adds_for(EmfWorld* w, uint32_t session, uint32_t entity, const OfferSpec& spec,
                                                 bool first, bool* range_added);
    ComponentChangeRequest change_of(EmfWorld* w, uint32_t session, uint32_t entity, uint32_t hash, ComponentOp op,
                                     std::vector<uint8_t> bytes, bool first);
    void submit(std::vector<ComponentChangeRequest>& reqs);  // outside the lock
    // The removes of an offer (the range check only when EMF added it).
    std::vector<ComponentChangeRequest> removes_for(EmfWorld* w, uint32_t session, uint32_t entity, bool range);
    // Its offer is the one EMF made from `spec` (and not a game offer on an id that is another entity's now).
    bool still_ours(EmfWorld* w, uint32_t entity, const OfferSpec& spec) const;
    void say(const std::string& line);
    uint32_t session() const { return deps_.session ? deps_.session() : 0; }

    Deps deps_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    OfferRegistry registry_;
    std::map<Key, State> state_;
    uint32_t session_seen_ = 0;
    int ticks_ = 0;
};

}  // namespace emf::core
