// change_component through the C API (v23): the table, the request's checks, and the hand-over to the service.
#include <windows.h>

#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "common/offsets.h"
#include "core/api.h"
#include "core/api_sizes.h"
#include "core/component_service.h"
#include "emf/generated/components.hpp"
#include "fake_world.h"

using namespace emf;
using namespace emf::core;
using emf::test::FakeWorld;

namespace {
namespace ecs = emf::gen::ecs;
std::vector<std::string> g_lines;
uint8_t* f_push(void*, void*, uint8_t, uint32_t, uint32_t) { return nullptr; }
void* f_get(void* out, void*, uint32_t, uint32_t) {
    auto* o = static_cast<uint64_t*>(out);
    o[0] = o[1] = 0;
    return out;
}
struct Fixture {
    CoreState state;
    FakeWorld server, client;
    std::unique_ptr<ComponentService> service;
    Fixture(bool on = true) {
        g_lines.clear();
        state.log_line = [](const std::string& s) { g_lines.push_back(s); };
        state.now_ms = [] { return uint64_t(1000); };
        server.put<uint8_t>(off::kKind, EMF_WORLD_SERVER);
        client.put<uint8_t>(off::kKind, EMF_WORLD_CLIENT);
        if (on) {
            ComponentService::Deps d;
            d.queue_push = f_push;
            d.get_component = f_get;
            d.session = [] { return 1u; };
            service = std::make_unique<ComponentService>(std::move(d));
            state.components = service.get();
        } else {
            state.components_off = "the QueuePush signature is not usable on this game build";
        }
        set_core_state(&state);
    }
    ~Fixture() { set_core_state(nullptr); }
    EmfWorld* w() { return reinterpret_cast<EmfWorld*>(server.p()); }
};
bool logged(const std::string& needle) {
    for (const auto& l : g_lines)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
EmfComponentChange scale_change(uint32_t entity, const float* value) {
    EmfComponentChange c{};
    c.size = sizeof c;
    c.op = EMF_COMPONENT_ADD;
    c.entity = entity;
    c.hash = ecs::ScaleComponent::kHash;
    c.data = value;
    c.data_size = value ? 4u : 0u;
    return c;
}
}  // namespace

TEST_CASE("api v23: the table has change_component after player_level_set") {
    const EmfApi* api = api_table();
    CHECK(api->version >= 23u);
    CHECK(offsetof(EmfApi, change_component) == kApiV22Size);
    CHECK(api->size >= kApiV23Size);
    CHECK(api->change_component != nullptr);
    CHECK(sizeof(EmfComponentChange) == kComponentChangeV23Size);
}

TEST_CASE("change_component: a ScaleComponent add reaches the service; bad requests are refused with a log line") {
    Fixture f;
    const EmfApi* api = api_table();
    const float big = 1.25f;
    EmfComponentChange c = scale_change(2000, &big);
    CHECK(api->change_component(f.w(), &c));
    CHECK(f.service->pending() == 1u);
    CHECK_FALSE(api->change_component(f.w(), nullptr));
    EmfComponentChange tiny = c;
    tiny.size = 8;
    CHECK_FALSE(api->change_component(f.w(), &tiny));
    EmfComponentChange bad_op = scale_change(2001, &big);
    bad_op.op = 7;
    CHECK_FALSE(api->change_component(f.w(), &bad_op));
    EmfComponentChange on_client = scale_change(2002, &big);
    CHECK_FALSE(api->change_component(reinterpret_cast<EmfWorld*>(f.client.p()), &on_client));
    EmfComponentChange wrong = scale_change(2003, &big);
    wrong.data_size = 8;
    CHECK_FALSE(api->change_component(f.w(), &wrong));
    CHECK(f.service->pending() == 1u);
    CHECK(logged("refused"));
    EmfComponentChange zeros = scale_change(2004, nullptr);  // NULL data: zeros
    CHECK(api->change_component(f.w(), &zeros));
}

TEST_CASE("change_component: off on this game build says why once") {
    Fixture f(false);
    const float big = 1.25f;
    EmfComponentChange c = scale_change(2000, &big);
    CHECK_FALSE(api_table()->change_component(f.w(), &c));
    CHECK(logged("QueuePush signature"));
}
