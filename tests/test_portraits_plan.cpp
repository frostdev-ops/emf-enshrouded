// Portraits at the held start: the "portraits" lists of mods' mod.json, what the cache lacks (at most 32 per start), and
// the request for emf_content_portraits (frames and size as JSON integers).
#include <doctest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "loader/portraits_plan.h"

using namespace emf::loader;

namespace {
const char* kWolf = "503fa1d1-dce2-45a1-85a5-185a22666d33";
const char* kBoar = "0bd8ba25-d743-425c-9947-147004b3cb9a";
}  // namespace

TEST_CASE("portraits plan: lists from mod.json, defaults, template or model; bad entries are skipped with a reason") {
    std::vector<std::pair<std::string, std::string>> manifests = {
        {"beastmaster", std::string("{\"id\": \"beastmaster\", \"portraits\": [") +
                            "{\"key\": \"wolf\", \"template\": \"" + kWolf + "\"}," +
                            "{\"key\": \"boar\", \"model\": \"" + kBoar + "\", \"frames\": 12, \"size\": 128}," +
                            "{\"key\": \"both\", \"template\": \"" + kWolf + "\", \"model\": \"" + kBoar + "\"}," +
                            "{\"key\": \"none\"}," +
                            "{\"key\": \"CON\", \"template\": \"" + kWolf + "\"}," +
                            "{\"key\": \"far\", \"template\": \"" + kWolf + "\", \"frames\": 65}," +
                            "{\"key\": \"tiny\", \"template\": \"" + kWolf + "\", \"size\": 4}," +
                            "{\"key\": \"bad\", \"template\": \"not-a-guid\"}]}"},
        {"plain", "{\"id\": \"plain\"}"},
        {"broken", "{not json"},
        {"other", std::string("{\"portraits\": [{\"key\": \"wolf\", \"template\": \"") + kBoar + "\"}]}"},
    };
    std::vector<std::string> problems;
    const auto items = read_portrait_lists(manifests, &problems);
    REQUIRE(items.size() == 2u);
    CHECK(items[0].mod == "beastmaster");
    CHECK(items[0].key == "wolf");
    CHECK(items[0].template_guid == kWolf);
    CHECK(items[0].frames == 24);
    CHECK(items[0].size == 256);
    CHECK(items[1].key == "boar");
    CHECK(items[1].model_guid == kBoar);
    CHECK(items[1].frames == 12);
    CHECK(items[1].size == 128);
    std::set<std::string> said;
    for (const auto& p : problems) said.insert(p.substr(0, p.find(':')));
    CHECK(problems.size() == 8u);  // both, none, CON, far, tiny, bad, broken, the second wolf
    CHECK(said.count("broken") == 1u);
    CHECK(said.count("other") == 1u);  // "wolf" is beastmaster's already, with another template
}

TEST_CASE("portraits plan: what the cache lacks, at most the cap per start, in list order") {
    std::vector<PortraitItem> items;
    for (int i = 0; i < 40; ++i) {
        PortraitItem p;
        p.key = "k" + std::to_string(i);
        items.push_back(p);
    }
    const auto missing = missing_portraits(items, [](const std::string& key) { return key == "k0" || key == "k5"; });
    REQUIRE(missing.size() == kMaxPortraitsPerStart);
    CHECK(missing[0].key == "k1");
    CHECK(missing[4].key == "k6");
    CHECK(missing_portraits(items, [](const std::string&) { return true; }).empty());
}

TEST_CASE("portraits plan: the request names the folders and gives frames and size as integers") {
    PortraitItem a;
    a.key = "wolf";
    a.template_guid = kWolf;
    PortraitItem b;
    b.key = "boar";
    b.model_guid = kBoar;
    b.frames = 12;
    b.size = 128;
    const std::string req = portraits_request(L"C:\\Game\\emf\\content\\client", L"C:\\Game\\emf\\cache\\portraits", {a, b});
    const auto parsed = emf::core::json::parse(req);
    REQUIRE(parsed.value);
    const auto& v = *parsed.value;
    REQUIRE(v.get("game_dir"));
    CHECK(v.get("game_dir")->s == "C:\\Game\\emf\\content\\client");
    CHECK(v.get("cache_dir")->s == "C:\\Game\\emf\\cache\\portraits");
    CHECK(v.get("file_name")->s == "enshrouded");
    REQUIRE(v.get("items"));
    REQUIRE(v.get("items")->arr.size() == 2u);
    const auto& w = v.get("items")->arr[0];
    CHECK(w.get("key")->s == "wolf");
    CHECK(w.get("template")->s == kWolf);
    CHECK(w.get("model") == nullptr);
    CHECK(req.find("\"frames\": 24") != std::string::npos);  // an integer, not 24.0
    CHECK(req.find("\"size\": 128") != std::string::npos);
    CHECK(v.get("items")->arr[1].get("model")->s == kBoar);
}
