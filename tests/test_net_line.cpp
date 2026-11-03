// Mod messages (API v27): the marked chat line, "#!emf1 <type> <seq> <channel>@<ver> <frag>/<count> <enc> <payload>",
// a message split into lines of at most 1024 bytes (text when it is printable ASCII, else base64url) and put back.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/net/net_line.h"

using namespace emf::core::net;

TEST_CASE("net line: a short text message is one line, written and read back the same") {
    std::string why;
    const auto lines = split_message('M', 5, "bm/pick", 1, "hello world", &why);
    REQUIRE(lines.size() == 1u);
    CHECK(lines[0].enc == 't');
    const std::string wire = encode_line(lines[0]);
    CHECK(wire == "#!emf1 M 5 bm/pick@1 0/1 t hello world");
    CHECK(is_marked(wire));
    const auto back = parse_line(wire, &why);
    REQUIRE(back.has_value());
    CHECK(back->type == 'M');
    CHECK(back->seq == 5u);
    CHECK(back->channel == "bm/pick");
    CHECK(back->version == 1u);
    CHECK(back->frag == 0u);
    CHECK(back->count == 1u);
    CHECK(join_payloads({*back}) == std::optional<std::string>("hello world"));
}

TEST_CASE("net line: sequence numbers are base36 on the wire") {
    NetLine l;
    l.seq = 36 * 36 + 35;  // "10z"
    l.channel = "a/b";
    l.payload = "x";
    const std::string wire = encode_line(l);
    CHECK(wire.find(" 10z ") != std::string::npos);
    std::string why;
    REQUIRE(parse_line(wire, &why).has_value());
    CHECK(parse_line(wire, &why)->seq == 36u * 36u + 35u);
}

TEST_CASE("net line: bytes that are not printable ASCII go as base64url and come back exactly") {
    std::string data;
    for (int i = 0; i < 256; ++i) data += static_cast<char>(i);
    std::string why;
    const auto lines = split_message('M', 0, "bm/state", 3, data, &why);
    REQUIRE_FALSE(lines.empty());
    for (const auto& l : lines) CHECK(l.enc == 'b');
    std::vector<NetLine> parsed;
    for (const auto& l : lines) {
        const auto p = parse_line(encode_line(l), &why);
        REQUIRE(p.has_value());
        parsed.push_back(*p);
    }
    CHECK(join_payloads(parsed) == std::optional<std::string>(data));
    CHECK(base64url_decode(base64url_encode(data)) == std::optional<std::string>(data));
    CHECK_FALSE(base64url_decode("ab+/").has_value());  // not the url alphabet
    // a space at a fragment's edge would not survive a chat line's trimming: such text goes as base64url too
    CHECK(split_message('M', 0, "bm/x", 1, " leading", &why)[0].enc == 'b');
}

TEST_CASE("net line: long messages are split into lines of at most 1024 bytes, up to 24, numbered on") {
    std::string why;
    const std::string text(4000, 'q');
    const auto lines = split_message('M', 10, "bm/long", 2, text, &why);
    REQUIRE(lines.size() >= 4u);
    for (size_t i = 0; i < lines.size(); ++i) {
        CHECK(encode_line(lines[i]).size() <= kMaxLine);
        CHECK(lines[i].seq == 10u + i);
        CHECK(lines[i].frag == i);
        CHECK(lines[i].count == lines.size());
    }
    CHECK(join_payloads(lines) == std::optional<std::string>(text));
    CHECK(split_message('M', 0, "bm/long", 2, std::string(22000, 'q'), &why).size() <= kMaxFragments);
    CHECK(split_message('M', 0, "bm/long", 2, std::string(30000, 'q'), &why).empty());  // more than 24 lines
    CHECK(why.find("24") != std::string::npos);
}

TEST_CASE("net line: malformed lines are refused with a reason") {
    std::string why;
    const char* bad[] = {
        "#!emf2 M 5 bm/pick@1 0/1 t hi",  // another protocol
        "#!emf1 X 5 bm/pick@1 0/1 t hi",  // unknown type
        "#!emf1 M 5! bm/pick@1 0/1 t hi", // seq not base36
        "#!emf1 M 5 bmpick@1 0/1 t hi",   // channel without '/'
        "#!emf1 M 5 bm/pick@0 0/1 t hi",  // version 0
        "#!emf1 M 5 bm/pick@70000 0/1 t hi",
        "#!emf1 M 5 bm/pick@1 1/1 t hi",  // frag >= count
        "#!emf1 M 5 bm/pick@1 0/25 t hi", // more than 24 fragments
        "#!emf1 M 5 bm/pick@1 0/1 x hi",  // unknown encoding
        "#!emf1 M 5 bm/pick@1 0/1 b a+b", // not base64url
        "#!emf1 M 5 bm/pick@1",           // fields missing
        "hello #!emf1 M 5 bm/pick@1 0/1 t hi",
    };
    for (const char* line : bad) {
        why.clear();
        const auto p = parse_line(line, &why);
        CHECK_MESSAGE(!p.has_value(), line);
        CHECK_MESSAGE(!why.empty(), line);
    }
    CHECK_FALSE(parse_line(std::string(kMaxLine + 1, 'a'), &why).has_value());
    CHECK_FALSE(is_marked("#!emf1"));
    CHECK_FALSE(is_marked("hello"));
}
