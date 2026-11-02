// API v21: world settings through the C API (world_setting_get/set/reset, on_world_setting_changed) and emf.hpp.
#include <windows.h>

#include <doctest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "core/api.h"
#include "core/api_sizes.h"
#include "core/world_settings_service.h"
#include "core/world_settings_state.h"
#include "emf/emf.hpp"
#include "world_settings_test_util.h"

using namespace emf::core;
using ws_test::bytes;
using ws_test::defaults;

namespace {
std::vector<std::string> g_log;
bool logged(const char* needle) {
    for (const auto& l : g_log)
        if (l.find(needle) != std::string::npos) return true;
    return false;
}
struct Fixture {
    CoreState state;
    ws::State ws_state;
    ws_test::GS live = defaults();
    ws::Service svc{ws_state, ws::ServiceDeps{[this] { return bytes(live); }, [] { return 1u; }}};
    Fixture() {
        g_log.clear();
        state.log_line = [](const std::string& s) { g_log.push_back(s); };
        state.world_settings = &svc;
        set_core_state(&state);
        ws_state.on_server_step(bytes(live), 1, 0);  // the session's base
    }
    ~Fixture() { set_core_state(nullptr); }
};
std::vector<std::string> g_changes;
void on_change(const char* key, double value, void* user) {
    g_changes.push_back(std::string(key) + "=" + std::to_string(static_cast<int>(value)) + (user ? "+u" : ""));
}
}  // namespace

TEST_CASE("api v21: the table has the world settings functions after v20's teleport") {
    const EmfApi* api = api_table();
    CHECK(api->version >= 21u);
    CHECK(EMF_API_VERSION >= 21);
    CHECK(offsetof(EmfApi, world_setting_get) == kApiV20Size);
    CHECK(kApiV21Size <= sizeof(EmfApi));  // later versions append after world_setting_info
    CHECK(api->world_setting_info != nullptr);
    CHECK(sizeof(EmfWorldSettingInfo) == 72);
    CHECK(api->world_setting_get != nullptr);
    CHECK(api->world_setting_set != nullptr);
    CHECK(api->world_setting_reset != nullptr);
    CHECK(api->on_world_setting_changed != nullptr);
    CHECK(EMF_WS_PERSIST == 1u);
}

TEST_CASE("api v21: get, set and reset go through the core's world settings; refusals are logged") {
    Fixture fx;
    const EmfApi* api = api_table();
    double v = 0;
    REQUIRE(api->world_setting_get("loot", &v));
    CHECK(v == doctest::Approx(1));
    CHECK(api->world_setting_set("day_minutes", 10, 0));
    CHECK(api->world_setting_set("resourceDropStackAmountFactor", 2, EMF_WS_PERSIST));
    fx.ws_state.on_server_step(bytes(fx.live), 1, 1000);
    CHECK(fx.live.dayTimeDuration.value == 600'000'000'000);
    CHECK(fx.live.resourceDropStackAmountFactor == doctest::Approx(2));
    CHECK(fx.ws_state.persisted().count("resourceDropStackAmountFactor") == 1);
    CHECK_FALSE(api->world_setting_set("loot", 10, 0));
    CHECK(logged("0.25 to 2"));
    CHECK_FALSE(api->world_setting_set("nope", 1, 0));
    CHECK_FALSE(api->world_setting_get("nope", &v));
    CHECK_FALSE(api->world_setting_set(nullptr, 1, 0));
    CHECK_FALSE(api->world_setting_set("loot", 2, 0x80));  // unknown flag bits
    CHECK(api->world_setting_reset("day_minutes", 0));
    fx.ws_state.on_server_step(bytes(fx.live), 1, 3000);
    CHECK(fx.live.dayTimeDuration.value == 1'800'000'000'000);
    CHECK(api->world_setting_reset(nullptr, EMF_WS_PERSIST));  // all, and forget the kept ones
    fx.ws_state.on_server_step(bytes(fx.live), 1, 5000);
    CHECK(fx.live.resourceDropStackAmountFactor == doctest::Approx(1));
    CHECK(fx.ws_state.persisted().empty());
}

TEST_CASE("api v21: a change callback gets each changed key after the server step's write") {
    Fixture fx;
    g_changes.clear();
    const EmfApi* api = api_table();
    int user = 0;
    REQUIRE(api->on_world_setting_changed(&on_change, &user));
    CHECK_FALSE(api->on_world_setting_changed(nullptr, nullptr));
    api->world_setting_set("xp_combat", 2, 0);
    fx.svc.notify(fx.ws_state.on_server_step(bytes(fx.live), 1, 1000));
    REQUIRE(g_changes.size() == 1);
    CHECK(g_changes[0] == "experienceCombatFactor=2+u");
}

TEST_CASE("api v21: without world settings (a test core, an old build) every call answers false") {
    CoreState state;
    state.log_line = [](const std::string&) {};
    set_core_state(&state);
    const EmfApi* api = api_table();
    double v = 0;
    CHECK_FALSE(api->world_setting_get("loot", &v));
    CHECK_FALSE(api->world_setting_set("loot", 2, 0));
    CHECK_FALSE(api->world_setting_reset(nullptr, 0));
    CHECK_FALSE(api->on_world_setting_changed(&on_change, nullptr));
    set_core_state(nullptr);
}

TEST_CASE("emf.hpp v21: world_setting, set_world_setting and reset_world_setting") {
    Fixture fx;
    emf::init(api_table());
    REQUIRE(emf::has_v21());
    REQUIRE(emf::world_setting("loot").has_value());
    CHECK(*emf::world_setting("loot") == doctest::Approx(1));
    CHECK_FALSE(emf::world_setting("nope").has_value());
    CHECK(emf::set_world_setting("loot", 1.5));
    fx.ws_state.on_server_step(bytes(fx.live), 1, 1000);
    CHECK(*emf::world_setting("loot") == doctest::Approx(1.5));
    CHECK(emf::reset_world_setting("loot"));
    fx.ws_state.on_server_step(bytes(fx.live), 1, 3000);
    CHECK(*emf::world_setting("loot") == doctest::Approx(1));
}

TEST_CASE("api v21: world_setting_info lists every setting with its range, now and the world's own value") {
    Fixture fx;
    const EmfApi* api = api_table();
    EmfWorldSettingInfo info{};
    info.size = sizeof info;
    uint32_t n = 0;
    bool saw_loot = false, saw_day = false;
    while (api->world_setting_info(n, &info)) {
        REQUIRE(info.key != nullptr);
        REQUIRE(info.alias != nullptr);
        REQUIRE(info.group != nullptr);
        if (std::string(info.alias) == "loot") {
            saw_loot = true;
            CHECK(info.type == EMF_WS_FLOAT);
            CHECK(info.min == doctest::Approx(0.25));
            CHECK(info.max == doctest::Approx(2));
            CHECK(info.value == doctest::Approx(1));
            CHECK(info.world == doctest::Approx(1));
            CHECK((info.flags & EMF_WS_CHANGED) == 0);
        }
        if (std::string(info.alias) == "day_minutes") saw_day = true;
        ++n;
    }
    CHECK(n == 38);
    CHECK(saw_loot);
    CHECK(saw_day);
    // after a change: now and the world's own value differ, and the setting is marked changed
    api->world_setting_set("day_minutes", 10, 0);
    fx.ws_state.on_server_step(bytes(fx.live), 1, 1000);
    for (uint32_t i = 0; api->world_setting_info(i, &info); ++i) {
        if (std::string(info.alias) != "day_minutes") continue;
        CHECK(info.type == EMF_WS_MINUTES);
        CHECK(info.value == doctest::Approx(10));
        CHECK(info.world == doctest::Approx(30));
        CHECK((info.flags & EMF_WS_CHANGED) != 0);
    }
    // a too small size field, no struct, or past the end: false
    EmfWorldSettingInfo tiny{};
    tiny.size = 8;
    CHECK_FALSE(api->world_setting_info(0, &tiny));
    CHECK_FALSE(api->world_setting_info(0, nullptr));
    CHECK_FALSE(api->world_setting_info(n, &info));
}

TEST_CASE("api v21: world_setting_info without a hosted world answers false") {
    CoreState state;
    state.log_line = [](const std::string&) {};
    ws::State st;
    ws::Service svc{st, ws::ServiceDeps{[] { return static_cast<uint8_t*>(nullptr); }, [] { return 0u; }}};
    state.world_settings = &svc;
    set_core_state(&state);
    EmfWorldSettingInfo info{};
    info.size = sizeof info;
    CHECK_FALSE(api_table()->world_setting_info(0, &info));
    set_core_state(nullptr);
}
