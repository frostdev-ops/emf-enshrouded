// Adding and removing components (EMF API v23): the curated list, the checks a request passes, the engine's records.
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/components_api.h"
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"

using namespace emf::core;
namespace gen = emf::gen::ecs;

TEST_CASE("components: the curated list matches the generated structs; ScaleComponent is not saved") {
    const ComponentRule* scale = component_rule(gen::ScaleComponent::kHash);
    REQUIRE(scale);
    CHECK(std::string(scale->name) == "ScaleComponent");
    CHECK(scale->hash == 0x2a0c9602u);
    CHECK(scale->size == 4);
    CHECK_FALSE(scale->savable);
    CHECK(scale->add);
    CHECK_FALSE(scale->remove);  // only when this mod added it (the ledger)
    const ComponentRule* tame = component_rule(gen::IsTame::kHash);
    REQUIRE(tame);
    CHECK(tame->savable);  // no do_not_save: it goes into the world save
    const ComponentRule* nosave = component_rule(gen::DoNotSave::kHash);
    REQUIRE(nosave);
    CHECK(nosave->add);
    CHECK(nosave->remove);
    CHECK(component_rule(gen::ConditionalDespawn::kHash)->remove);
    CHECK_FALSE(component_rule(gen::ConditionalDespawn::kHash)->add);
    CHECK(component_rule(gen::Teleport::kHash) == nullptr);  // its own API
    CHECK(component_rule(gen::CurrentTransform::kHash) == nullptr);
    CHECK(component_rule(0) == nullptr);
    CHECK(component_rule_by_name("ScaleComponent") == scale);
    CHECK(component_rule_by_name("scalecomponent") == scale);  // any case
    CHECK(component_rule_by_name("keen::ecs::ScaleComponent") == scale);
    CHECK(component_rule_by_name("Inventory") == nullptr);
}

TEST_CASE("check_change: what a mod may add or remove, with what size and flags") {
    // ScaleComponent: an add with its 4 bytes (or none: zeros), no flag needed
    CHECK(check_change(ComponentOp::Add, gen::ScaleComponent::kHash, 4, true, 0, false).empty());
    CHECK(check_change(ComponentOp::Add, gen::ScaleComponent::kHash, 0, false, 0, false).empty());
    CHECK(check_change(ComponentOp::Add, gen::ScaleComponent::kHash, 8, true, 0, false).find("size") !=
          std::string::npos);
    // removing it: only after this mod added it this session
    CHECK(check_change(ComponentOp::Remove, gen::ScaleComponent::kHash, 0, false, 0, false).find("not allowed") !=
          std::string::npos);
    CHECK(check_change(ComponentOp::Remove, gen::ScaleComponent::kHash, 0, false, 0, true).empty());
    // savable ones need EMF_COMPONENT_PERSISTENT
    CHECK(check_change(ComponentOp::Add, gen::IsTame::kHash, 1, true, 0, false).find("PERSISTENT") != std::string::npos);
    CHECK(check_change(ComponentOp::Add, gen::IsTame::kHash, 1, true, EMF_COMPONENT_PERSISTENT, false).empty());
    CHECK(check_change(ComponentOp::Remove, gen::DoNotSave::kHash, 0, false, 0, false).find("PERSISTENT") !=
          std::string::npos);
    CHECK(check_change(ComponentOp::Remove, gen::DoNotSave::kHash, 0, false, EMF_COMPONENT_PERSISTENT, false).empty());
    // anything else, and the expert flag (reserved), are refused
    CHECK(check_change(ComponentOp::Add, gen::Inventory::kHash, sizeof(gen::Inventory), true, 0, false)
              .find("not one EMF lets mods add") != std::string::npos);
    CHECK(check_change(ComponentOp::Add, gen::ScaleComponent::kHash, 4, true, EMF_COMPONENT_EXPERT, false)
              .find("EXPERT") != std::string::npos);
    CHECK(check_change(ComponentOp::Add, gen::ScaleComponent::kHash, 4, true, 0x80, false).find("flag") !=
          std::string::npos);
    CHECK_FALSE(check_change(ComponentOp::Add, 0, 0, false, 0, false).empty());
}

TEST_CASE("component records: kind 3 adds with hash, size and data; kind 4 removes by hash") {
    gen::ScaleComponent s{};
    s.uniformScale = 1.25f;
    const auto add = add_component_record(gen::ScaleComponent::kHash, &s, sizeof s);
    REQUIRE(add.size() == 0x6C + 4u);
    uint32_t hash = 0, other = 1;
    uint16_t size = 0;
    float scale = 0;
    std::memcpy(&hash, add.data() + 0x0C, 4);
    std::memcpy(&other, add.data() + 0x10, 4);
    std::memcpy(&size, add.data() + 0x4C, 2);
    std::memcpy(&scale, add.data() + 0x6C, 4);
    CHECK(hash == gen::ScaleComponent::kHash);
    CHECK(other == 0u);  // the other 15 slots are empty
    CHECK(size == 4);
    CHECK(scale == 1.25f);
    const auto zeros = add_component_record(gen::IsTame::kHash, nullptr, 1);
    REQUIRE(zeros.size() == 0x6C + 1u);
    CHECK(zeros[0x6C] == 0);
    const auto rem = remove_component_record(gen::DoNotSave::kHash);
    REQUIRE(rem.size() == 0x4C);
    std::memcpy(&hash, rem.data() + 0x0C, 4);
    CHECK(hash == gen::DoNotSave::kHash);
}
