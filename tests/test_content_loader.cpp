// The content loader (docs/engine_parts/content_loader.md): the parts in src/loader/ that decide whether EMF builds the
// EML mods' content into a shadow copy of the game's data, build it, and remember what was built.
#include <doctest.h>

#include <string>

#include "content_fixtures.h"
#include "loader/kfc_header.h"

using content_fixtures::kfc_bytes;

TEST_CASE("kfc header: synthetic header") {
    const auto h = emf::loader::parse_kfc_header(kfc_bytes("1|x|y", 3));
    REQUIRE(h.has_value());
    CHECK(h->version == "1|x|y");
    CHECK(h->containers == 3);
}

TEST_CASE("kfc header: wrong magic") {
    std::string b = kfc_bytes("1|x|y", 3);
    b[3] = '5';  // "KFC5"
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
}

TEST_CASE("kfc header: version past the end") {
    std::string b = kfc_bytes("1|x|y", 3);
    content_fixtures::put_u32(b, 20, 100);  // a 100-byte version in 64 bytes
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
    b = kfc_bytes("1|x|y", 3);
    content_fixtures::put_u32(b, 16, 0xFFFFFFF0u);  // an offset that wraps around
    CHECK_FALSE(emf::loader::parse_kfc_header(b).has_value());
}

TEST_CASE("kfc header: truncated") {
    CHECK_FALSE(emf::loader::parse_kfc_header(kfc_bytes("1|x|y", 3).substr(0, 20)).has_value());
    CHECK_FALSE(emf::loader::parse_kfc_header("").has_value());
}
