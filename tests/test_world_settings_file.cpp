// World settings: the overrides kept for a world (one text file per world slot, emf\config\world_settings\<slot>.txt).
#include <doctest.h>

#include <map>
#include <string>
#include <vector>

#include "core/world_settings_file.h"

using namespace emf::core::ws;

TEST_CASE("world settings file: round trip") {
    const std::map<std::string, double> in = {{"resourceDropStackAmountFactor", 2.0}, {"dayTimeDuration", 10.0},
                                              {"weatherFrequency", 0.0}};
    const std::string t = overrides_text(in);
    CHECK(t.find("dayTimeDuration=10") != std::string::npos);
    CHECK(t.find("dayTimeDuration") < t.find("resourceDropStackAmountFactor"));  // sorted
    std::vector<std::string> skipped;
    CHECK(parse_overrides(t, &skipped) == in);
    CHECK(skipped.empty());
}

TEST_CASE("world settings file: unknown keys and bad values are skipped and named") {
    std::vector<std::string> skipped;
    const auto got = parse_overrides("resourceDropStackAmountFactor=2\nnoSuchSetting=1\ndayTimeDuration=0\nweather=Rare\n"
                                     "experienceCombatFactor=abc\n",
                                     &skipped);
    CHECK(got.size() == 2);
    CHECK(got.at("resourceDropStackAmountFactor") == doctest::Approx(2));
    CHECK(got.at("weatherFrequency") == doctest::Approx(1));  // an alias and an enum name are understood
    REQUIRE(skipped.size() == 3);
    CHECK(skipped[0].find("noSuchSetting") != std::string::npos);
    CHECK(skipped[1].find("dayTimeDuration") != std::string::npos);
    CHECK(skipped[2].find("experienceCombatFactor") != std::string::npos);
}

TEST_CASE("world settings file: empty or comment-only text is no overrides") {
    std::vector<std::string> skipped;
    CHECK(parse_overrides("", &skipped).empty());
    CHECK(parse_overrides("# EMF world settings kept for this world\r\n\r\n", &skipped).empty());
    CHECK(skipped.empty());
    CHECK(overrides_text({}).find('=') == std::string::npos);
}
