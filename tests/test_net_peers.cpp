// Mod messages (API v27): lines back into messages per peer (in order, no partial or double delivery), the server's
// peers (a hello makes one; a new handle for the same account replaces it; a reused handle is not the old peer), and
// the rate limits.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/net/net_line.h"
#include "core/net/net_peers.h"

using namespace emf::core::net;

namespace {
std::vector<NetLine> msg(uint32_t seq, const std::string& data, const char* channel = "bm/state") {
    std::string why;
    return split_message('M', seq, channel, 1, data, &why);
}
}  // namespace

TEST_CASE("net reassembly: one-line and many-line messages come out whole, once, in order") {
    Reassembly r;
    std::string why;
    auto one = r.add(msg(0, "hi")[0], &why);
    REQUIRE(one.has_value());
    CHECK(one->data == "hi");
    CHECK(one->channel == "bm/state");
    const std::string big(3000, 'z');
    const auto lines = msg(1, big);
    REQUIRE(lines.size() >= 3u);
    for (size_t i = 0; i + 1 < lines.size(); ++i) CHECK_FALSE(r.add(lines[i], &why).has_value());
    auto whole = r.add(lines.back(), &why);
    REQUIRE(whole.has_value());
    CHECK(whole->data == big);
    CHECK_FALSE(r.add(lines.back(), &why).has_value());  // the same line again: a duplicate
    CHECK(why.find("duplicate") != std::string::npos);
}

TEST_CASE("net reassembly: a gap drops the partial message; the next message after it still comes") {
    Reassembly r;
    std::string why;
    const auto lines = msg(0, std::string(3000, 'a'));
    REQUIRE(lines.size() >= 3u);
    CHECK_FALSE(r.add(lines[0], &why).has_value());
    CHECK_FALSE(r.add(lines[2], &why).has_value());  // lines[1] was lost
    CHECK(why.find("gap") != std::string::npos);
    const uint32_t next = lines.back().seq + 1;
    CHECK_FALSE(r.add(lines.back(), &why).has_value());  // the rest of the broken message is dropped
    auto ok = r.add(msg(next, "after")[0], &why);
    REQUIRE(ok.has_value());
    CHECK(ok->data == "after");
}

TEST_CASE("net reassembly: a line that is not the first of a message, with nothing pending, is dropped") {
    Reassembly r;
    std::string why;
    const auto lines = msg(7, std::string(3000, 'a'));
    CHECK_FALSE(r.add(lines[1], &why).has_value());
    auto ok = r.add(msg(lines.back().seq + 1, "x")[0], &why);
    REQUIRE(ok.has_value());
}

TEST_CASE("net reassembly: a fragment whose type, version or encoding differs from the one before it drops the message") {
    const auto lines = msg(0, std::string(3000, 'a'));
    REQUIRE(lines.size() >= 3u);
    for (int field = 0; field < 3; ++field) {
        CAPTURE(field);
        Reassembly r;
        std::string why;
        CHECK_FALSE(r.add(lines[0], &why).has_value());
        NetLine odd = lines[1];
        if (field == 0) odd.type = 'W';
        if (field == 1) odd.version += 1;
        if (field == 2) odd.enc = odd.enc == 't' ? 'b' : 't';  // "aaaa" also reads as base64: it would decode, wrongly
        CHECK_FALSE(r.add(odd, &why).has_value());
        CHECK(why.find("without the ones before") != std::string::npos);
        for (size_t i = 2; i < lines.size(); ++i) CHECK_FALSE(r.add(lines[i], &why).has_value());  // nothing comes out
        auto ok = r.add(msg(lines.back().seq + 1, "after")[0], &why);  // and the next message is read again
        REQUIRE(ok.has_value());
        CHECK(ok->data == "after");
    }
}

TEST_CASE("net peers: a hello makes a peer; a new handle for the account replaces it; a reused handle is not it") {
    PeerTable t;
    t.hello(3, 0xAAA, {{"bm/state", 1}, {"bm/pick", 2}});
    REQUIRE(t.find(3, 0xAAA) != nullptr);
    CHECK(t.find(3, 0xAAA)->channels.at("bm/pick") == 2u);
    CHECK(t.find(3, 0xBBB) == nullptr);  // another account on that handle: not this peer
    t.hello(5, 0xAAA, {{"bm/state", 1}});  // the player came back on a new connection
    CHECK(t.find(3, 0xAAA) == nullptr);
    REQUIRE(t.find(5, 0xAAA) != nullptr);
    CHECK(t.size() == 1u);
    t.hello(3, 0xBBB, {});  // someone else got handle 3
    CHECK(t.size() == 2u);
    t.bye(5);
    CHECK(t.find(5, 0xAAA) == nullptr);
    CHECK(t.size() == 1u);
    auto* p = t.find(3, 0xBBB);
    REQUIRE(p);
    CHECK(p->next_seq() == 0u);
    CHECK(p->next_seq() == 1u);  // the sending side numbers every line it sends this peer
}

TEST_CASE("net rate limit: a burst, then the refill rate") {
    RateLimit r(10.0, 20.0);  // 10 a second, 20 at once
    int taken = 0;
    for (int i = 0; i < 30; ++i) taken += r.take(1000) ? 1 : 0;
    CHECK(taken == 20);
    CHECK_FALSE(r.take(1000));
    CHECK(r.take(1100));        // 100 ms later: one more
    CHECK_FALSE(r.take(1100));
    int later = 0;
    for (int i = 0; i < 30; ++i) later += r.take(10000) ? 1 : 0;  // long after: full again, not more
    CHECK(later == 20);
    CHECK(r.take(20000, 5.0));   // weighted (bytes)
    CHECK_FALSE(r.take(20000, 16.0));
}
