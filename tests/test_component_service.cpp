// change_component (API v23): the emf_components program pushing the engine's add/remove-components records, the checks
// after a step, the verify and the callbacks, over a fake QueuePush and a flush that applies the records.
#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/component_service.h"
#include "core/system_programs.h"
#include "emf/generated/components.hpp"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeProgramCtx;
using emf::test::FakeWorld;

namespace {
namespace ecs = emf::gen::ecs;

// entity -> component hash -> its bytes (CurrentTransform marks an entity that exists)
std::map<uint32_t, std::map<uint32_t, std::vector<uint8_t>>> g_ents;

void* f_get(void* out, void*, uint32_t hash, uint32_t entity) {
    auto* o = static_cast<uint64_t*>(out);
    o[0] = o[1] = 0;
    auto e = g_ents.find(entity);
    if (e == g_ents.end()) return out;
    auto c = e->second.find(hash);
    if (c == e->second.end()) return out;
    o[0] = reinterpret_cast<uint64_t>(c->second.data());
    o[1] = c->second.size();
    return out;
}

struct Pushed {
    uint8_t kind;
    uint32_t size, entity;
    uint8_t* rec;
};
std::vector<Pushed> g_pushes;
alignas(16) uint8_t g_records[64 * 1024];
size_t g_used = 0;
bool g_full = false, g_ignore = false;
uint8_t g_cmdbuf[0x40];
uint8_t* f_queue_push(void* cb, void* buf, uint8_t kind, uint32_t size, uint32_t entity) {
    CHECK(cb == g_cmdbuf);
    CHECK(buf == g_cmdbuf + 0x10);
    if (g_full) return nullptr;
    uint8_t* rec = g_records + g_used;
    g_used += (size + 15) & ~size_t(15);
    std::memset(rec, 0, size);
    rec[2] = kind;
    g_pushes.push_back({kind, size, entity, rec});
    return rec;
}
uint64_t f_register(void*, const void*, char) { return 1; }

// the end of a step: the engine applies the records (an add only when absent)
void flush() {
    if (!g_ignore)
        for (const auto& p : g_pushes) {
            uint32_t hash = 0;
            std::memcpy(&hash, p.rec + kRecHashes, 4);
            auto e = g_ents.find(p.entity);
            if (e == g_ents.end()) continue;
            if (p.kind == kRecAddComponents && !e->second.count(hash)) {
                uint16_t size = 0;
                std::memcpy(&size, p.rec + kRecSizes, 2);
                e->second[hash].assign(p.rec + kRecData, p.rec + kRecData + size);
            }
            if (p.kind == kRecRemoveComponents) e->second.erase(hash);
        }
    g_pushes.clear();
    g_used = 0;
}

uint32_t g_session = 1;
std::vector<std::string> g_log;
size_t count_logged(const std::string& needle) {
    size_t n = 0;
    for (const auto& l : g_log) n += l.find(needle) != std::string::npos;
    return n;
}

struct Answer {
    uint32_t status;
    std::string why;
    EmfWorld* world;  // NULL when the session ended first (the world may be gone)
};

struct Fixture {
    FakeWorld w;
    SystemPrograms programs;
    ComponentService service;
    FakeProgramCtx ctx;
    std::vector<Answer> answers;
    size_t pushed_total = 0;
    uint32_t tick = 100;
    Fixture() : service(deps()), ctx(w.p()) {
        programs.reset();
        g_ents.clear();
        g_pushes.clear();
        g_used = 0;
        g_full = g_ignore = false;
        g_session = 1;
        g_log.clear();
        std::memset(g_cmdbuf, 0, sizeof g_cmdbuf);
        const uint64_t cap = 1u << 20;
        std::memcpy(g_cmdbuf + kCmdBufCapacity, &cap, 8);
        uint8_t* cb = g_cmdbuf;
        std::memcpy(ctx.job + kJobCmdBuf, &cb, 8);
        w.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        w.put<uint32_t>(off::kTick, tick);
        REQUIRE(programs.add(service.spec(), true));
        REQUIRE(programs.register_into(w.p(), f_register) == 1);
        g_ents[2000][ecs::CurrentTransform::kHash].assign(sizeof(ecs::CurrentTransform), 0);  // a wolf
        g_ents[1][ecs::CurrentTransform::kHash].assign(sizeof(ecs::CurrentTransform), 0);     // the player
        g_ents[1][ecs::PlayerState::kHash].assign(sizeof(ecs::PlayerState), 0);
    }
    ~Fixture() { programs.reset(); }
    static ComponentService::Deps deps() {
        ComponentService::Deps d;
        d.queue_push = f_queue_push;
        d.get_component = f_get;
        d.log = [](const std::string& s) { g_log.push_back(s); };
        d.now_ms = [] { return uint64_t(1000); };
        d.session = [] { return g_session; };
        return d;
    }
    EmfWorld* world() { return reinterpret_cast<EmfWorld*>(w.p()); }
    ComponentChangeRequest add_scale(uint32_t entity, float scale, const char* mod = "beastmaster") {
        ComponentChangeRequest r;
        r.world = world();
        r.entity = entity;
        r.hash = ecs::ScaleComponent::kHash;
        r.op = ComponentOp::Add;
        r.data.resize(sizeof(ecs::ScaleComponent));
        std::memcpy(r.data.data(), &scale, 4);
        r.mod = mod;
        r.done = [this](EmfWorld* wd, uint32_t status, const std::string& why) { answers.push_back({status, why, wd}); };
        return r;
    }
    ComponentChangeRequest remove_scale(uint32_t entity, const char* mod = "beastmaster") {
        ComponentChangeRequest r = add_scale(entity, 0, mod);
        r.op = ComponentOp::Remove;
        r.data.clear();
        return r;
    }
    // one server step: emf_components, the flush, then EMF's post-step work
    void step() {
        system_program_desc(0).update(ctx.p());
        pushed_total += g_pushes.size();
        flush();
        ++tick;
        w.put<uint32_t>(off::kTick, tick);
        service.on_server_tick(world());
    }
    void steps(int n) {
        for (int i = 0; i < n; ++i) step();
    }
    float scale_of(uint32_t entity) {
        float f = 0;
        std::memcpy(&f, g_ents[entity][ecs::ScaleComponent::kHash].data(), 4);
        return f;
    }
};
}  // namespace

TEST_CASE("change_component: an add is checked after a step, pushed in the next, and answered done when it is there") {
    Fixture f;
    std::string why;
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), &why));
    f.step();  // checked: pushed next step
    CHECK(f.pushed_total == 0);
    CHECK(f.answers.empty());
    f.step();
    CHECK(f.pushed_total == 1);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_DONE);
    CHECK(f.answers[0].world == f.world());
    CHECK(f.scale_of(2000) == 1.25f);
    CHECK(f.service.pending() == 0);
}

TEST_CASE("change_component: an internal request (EMF's own E prompts) adds a component no mod may, with its bytes") {
    Fixture f;
    ComponentChangeRequest r;
    r.world = f.world();
    r.entity = 2000;
    r.hash = ecs::InteractionOfferRuntime::kHash;
    r.op = ComponentOp::Add;
    r.data.assign(sizeof(ecs::InteractionOfferRuntime), 0);
    r.data[8] = 0xBF;  // defaultSequence's low byte
    r.mod = "emf";
    r.done = [&f](EmfWorld* wd, uint32_t status, const std::string& why) { f.answers.push_back({status, why, wd}); };
    CHECK_FALSE(f.service.request(r, nullptr));  // as a mod's request: not on the list
    r.internal = true;
    REQUIRE(f.service.request(r, nullptr));
    f.steps(2);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_DONE);
    REQUIRE(g_ents[2000].count(ecs::InteractionOfferRuntime::kHash) == 1);
    CHECK(g_ents[2000][ecs::InteractionOfferRuntime::kHash][8] == 0xBF);
    r.entity = 1;
    CHECK_FALSE(f.service.request(r, nullptr));  // never a player, internal or not
    r.entity = 2000;
    r.hash = ecs::ClientInteractionOffer::kHash;
    r.data.clear();
    CHECK_FALSE(f.service.request(r, nullptr));  // an internal add brings its bytes (no list entry gives a size)
    r.op = ComponentOp::Remove;
    r.hash = ecs::InteractionOfferRuntime::kHash;
    REQUIRE(f.service.request(r, nullptr));  // an internal remove needs no record of who added it
    f.steps(2);
    CHECK(g_ents[2000].count(ecs::InteractionOfferRuntime::kHash) == 0);
}

TEST_CASE("change_component: a component already there answers already, nothing pushed; a remove of an absent one too") {
    Fixture f;
    g_ents[2000][ecs::ScaleComponent::kHash].assign(4, 0);
    REQUIRE(f.service.request(f.add_scale(2000, 2.0f), nullptr));
    f.steps(3);
    CHECK(f.pushed_total == 0);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_ALREADY);
    CHECK(f.scale_of(2000) == 0.0f);  // never overwritten
}

TEST_CASE("change_component: what a mod added it may remove again; another mod may not") {
    Fixture f;
    REQUIRE(f.service.request(f.add_scale(2000, 1.1f), nullptr));
    f.steps(2);
    std::string why;
    CHECK_FALSE(f.service.request(f.remove_scale(2000, "other_mod"), &why));
    CHECK(why.find("not allowed") != std::string::npos);
    REQUIRE(f.service.request(f.remove_scale(2000), &why));
    f.steps(2);
    REQUIRE(f.answers.size() == 2);
    CHECK(f.answers[1].status == EMF_COMPONENT_DONE);
    CHECK(g_ents[2000].count(ecs::ScaleComponent::kHash) == 0);
}

TEST_CASE("change_component: the entity gone before the push answers failed, with no push into its slot") {
    Fixture f;
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), nullptr));
    f.step();  // checked
    g_ents.erase(2000);
    f.step();
    CHECK(f.pushed_total == 0);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[0].why.find("gone") != std::string::npos);
}

TEST_CASE("change_component: refused at once: a client world, a player, a component not listed, a second change of the pair") {
    Fixture f;
    std::string why;
    FakeWorld client;
    client.put<uint8_t>(off::kKind, EMF_WORLD_CLIENT);
    ComponentChangeRequest c = f.add_scale(2000, 1.2f);
    c.world = reinterpret_cast<EmfWorld*>(client.p());
    CHECK_FALSE(f.service.request(c, &why));
    CHECK(why.find("server world") != std::string::npos);
    CHECK_FALSE(f.service.request(f.add_scale(1, 1.2f), &why));
    CHECK(why.find("player") != std::string::npos);
    ComponentChangeRequest inv = f.add_scale(2000, 1.2f);
    inv.hash = ecs::Inventory::kHash;
    inv.data.assign(sizeof(ecs::Inventory), 0);
    CHECK_FALSE(f.service.request(inv, &why));
    REQUIRE(f.service.request(f.add_scale(2000, 1.2f), &why));
    CHECK_FALSE(f.service.request(f.add_scale(2000, 1.3f), &why));
    CHECK(why.find("under way") != std::string::npos);
    CHECK(f.answers.empty());  // refusals at once never call back
    CHECK(count_logged("refused") >= 4);
}

TEST_CASE("change_component: a player found only by its PlayerState after the step is refused then") {
    Fixture f;
    g_ents[5000][ecs::CurrentTransform::kHash].assign(sizeof(ecs::CurrentTransform), 0);
    g_ents[5000][ecs::PlayerState::kHash].assign(sizeof(ecs::PlayerState), 0);
    REQUIRE(f.service.request(f.add_scale(5000, 1.2f), nullptr));
    f.step();
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[0].why.find("player") != std::string::npos);
}

TEST_CASE("change_component: a new world answers what is under way failed; nothing is pushed into it later") {
    Fixture f;
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), nullptr));
    f.step();
    f.service.world_reset();
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[0].world == nullptr);  // the old world may be freed: callers never see it
    f.steps(3);
    CHECK(f.pushed_total == 0);
    CHECK(f.answers.size() == 1);
    // and a session change seen at the next step does the same
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), nullptr));
    g_session = 2;
    f.step();
    REQUIRE(f.answers.size() == 2);
    CHECK(f.answers[1].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[1].world == nullptr);
    CHECK(f.pushed_total == 0);
    // end_session too
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), nullptr));
    f.service.end_session("back to the menu");
    REQUIRE(f.answers.size() == 3);
    CHECK(f.answers[2].world == nullptr);
}

TEST_CASE("change_component: a full command buffer waits; failed after 120 steps; a change the game ignores fails too") {
    Fixture f;
    g_full = true;
    REQUIRE(f.service.request(f.add_scale(2000, 1.25f), nullptr));
    f.steps(10);
    CHECK(f.answers.empty());
    g_full = false;
    f.steps(2);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_DONE);

    g_full = true;
    REQUIRE(f.service.request(f.remove_scale(2000), nullptr));
    f.steps(130);
    REQUIRE(f.answers.size() == 2);
    CHECK(f.answers[1].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[1].why.find("full") != std::string::npos);

    g_full = false;
    g_ignore = true;  // the engine drops the record
    REQUIRE(f.service.request(f.remove_scale(2000), nullptr));
    f.steps(6);
    REQUIRE(f.answers.size() == 3);
    CHECK(f.answers[2].status == EMF_COMPONENT_FAILED);
    CHECK(f.answers[2].why.find("did not apply") != std::string::npos);
}

TEST_CASE("change_component: an add without data adds zeros of the component's size; never a ScaleComponent of 0") {
    Fixture f;
    ComponentChangeRequest r = f.add_scale(2000, 0);
    r.hash = ecs::InteractionPause::kHash;
    r.data.clear();
    REQUIRE(f.service.request(r, nullptr));
    f.steps(2);
    REQUIRE(f.answers.size() == 1);
    CHECK(f.answers[0].status == EMF_COMPONENT_DONE);
    CHECK(g_ents[2000][ecs::InteractionPause::kHash].size() == sizeof(ecs::InteractionPause));
    std::string why;
    ComponentChangeRequest zero = f.add_scale(2000, 0);
    CHECK_FALSE(f.service.request(zero, &why));
    CHECK(why.find("0.1") != std::string::npos);
    zero.data.clear();  // no data: zeros
    CHECK_FALSE(f.service.request(zero, nullptr));
    CHECK(f.service.pending() == 0u);
}
