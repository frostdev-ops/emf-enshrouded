// API v27, a player's game: the outbox flush detour sends the mod message lines with chatClientSend before the game
// flushes, and the chatLogAdd detour takes a marked line the client's own chat gets (never added), with the game's
// functions faked.
#include <doctest.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "core/server_chat.h"

using namespace emf::core;

namespace {

struct FakeClient {
    std::vector<uint8_t> game_client = std::vector<uint8_t>(0x200, 0);
    std::vector<uint8_t> sys = std::vector<uint8_t>(0x200, 0);
    FakeClient() {
        uint8_t* p = sys.data();
        std::memcpy(game_client.data() + kGameClientChat, &p, 8);
    }
};

FakeClient* g_fc = nullptr;
ClientChatDetours g_cd{};
std::vector<std::string> g_waiting;          // lines the net service has to send
std::vector<std::string> g_sent;             // chatClientSend calls
std::vector<std::string> g_added;            // lines the game's chatLogAdd got
std::vector<std::pair<std::string, uint64_t>> g_taken;  // marked lines the net service took
std::vector<std::string> g_log;
std::vector<std::string> g_events;           // the order of the game's flush and the sends
uint8_t g_send_code = 0;                     // chatClientSend's result code
size_t g_send_fail_at = ~size_t(0);          // the call that answers "pool full"
bool g_take_marked = true;
uint8_t* g_send_sys = nullptr;
uint8_t g_send_kind = 255;

uint8_t* __fastcall fake_send(uint8_t* out, uint8_t* sys, uint8_t kind, const ChatText* t, uint32_t tag1, uint32_t tag2) {
    g_send_sys = sys;
    g_send_kind = kind;
    CHECK(tag1 == 0);
    CHECK(tag2 == 0);
    const size_t n = g_sent.size();
    g_events.push_back("send");
    if (n == g_send_fail_at) {
        out[0] = 2;  // pool full
        return out;
    }
    g_sent.emplace_back(t->data, static_cast<size_t>(t->size));
    out[0] = g_send_code;
    return out;
}

void __fastcall fake_flush(uint8_t*) { g_events.push_back("flush"); }

uint8_t* __fastcall fake_add(uint8_t* out, uint8_t*, uint8_t, const ChatText* t, uint32_t, uint32_t, uint32_t, uint64_t,
                             const void*) {
    g_added.emplace_back(t->data, static_cast<size_t>(t->size));
    out[0] = 0;
    return out;
}

struct Rig {
    FakeClient fc;
    Rig() {
        g_fc = &fc;
        g_waiting.clear();
        g_sent.clear();
        g_added.clear();
        g_taken.clear();
        g_log.clear();
        g_events.clear();
        g_send_code = 0;
        g_send_fail_at = ~size_t(0);
        g_take_marked = true;
        ClientChatDeps d;
        d.line = [](std::string_view text, uint64_t speaker) {
            g_taken.emplace_back(std::string(text), speaker);
            return g_take_marked;
        };
        d.take = [](size_t max) {
            const size_t n = (std::min)(max, g_waiting.size());
            std::vector<std::string> out(g_waiting.begin(), g_waiting.begin() + static_cast<std::ptrdiff_t>(n));
            g_waiting.erase(g_waiting.begin(), g_waiting.begin() + static_cast<std::ptrdiff_t>(n));
            return out;
        };
        d.put_back = [](std::vector<std::string> lines) { g_waiting.insert(g_waiting.begin(), lines.begin(), lines.end()); };
        d.log = [](const std::string& t) { g_log.push_back(t); };
        g_cd = client_chat_for_test(fake_flush, fake_send, fake_add, std::move(d));
    }
    ~Rig() {
        g_fc = nullptr;
        // leave the shared detour state off for the other tests
        server_chat_for_test(nullptr, nullptr, ServerChatDeps{});
    }
    void flush() { g_cd.flush(fc.game_client.data()); }
    // a line the game's client receive adds to the client's own chat
    bool receive(const std::string& text, uint64_t speaker, uint8_t* sys = nullptr) {
        const ChatText t{text.data(), text.size()};
        uint8_t res[16];
        std::memset(res, 0xFF, sizeof res);
        struct {
            uint64_t time, has;
        } when{0, 0};
        const size_t before = g_added.size();
        g_cd.log_add(res, sys ? sys : fc.sys.data(), 0, &t, 0, 0, 0x40, speaker, &when);
        CHECK(res[0] == 0);
        return g_added.size() == before;  // true: the game did not get it
    }
};

}  // namespace

TEST_CASE("client chat: lines are sent with chatClientSend, kind 0, on the client's chat system, before the game flushes") {
    Rig r;
    g_waiting = {"#!emf1 H 0 emf/net@1 0/1 t p1;x;1;1;", "#!emf1 M 1 bm/pick@1 0/1 t 3"};
    r.flush();
    REQUIRE(g_sent.size() == 2);
    CHECK(g_sent[0] == "#!emf1 H 0 emf/net@1 0/1 t p1;x;1;1;");
    CHECK(g_sent[1] == "#!emf1 M 1 bm/pick@1 0/1 t 3");
    CHECK(g_send_sys == r.fc.sys.data());
    CHECK(g_send_kind == 0);
    CHECK(g_events == std::vector<std::string>{"send", "send", "flush"});
    CHECK(g_waiting.empty());
}

TEST_CASE("client chat: at most four lines a frame; the rest wait for the next") {
    Rig r;
    for (int i = 0; i < 10; ++i) g_waiting.push_back("line " + std::to_string(i));
    r.flush();
    CHECK(g_sent.size() == kClientLinesPerFrame);
    CHECK(g_waiting.size() == 10 - kClientLinesPerFrame);
    r.flush();
    r.flush();
    CHECK(g_sent.size() == 10);
    CHECK(g_sent[9] == "line 9");
}

TEST_CASE("client chat: a full chat pool puts the line and those after it back, in order, for the next frame") {
    Rig r;
    g_waiting = {"a", "b", "c", "d"};
    g_send_fail_at = 2;  // "c" is refused with code 2
    r.flush();
    CHECK(g_sent == std::vector<std::string>{"a", "b"});
    CHECK(g_waiting == std::vector<std::string>{"c", "d"});
    g_send_fail_at = ~size_t(0);
    r.flush();
    CHECK(g_sent == std::vector<std::string>{"a", "b", "c", "d"});
    CHECK(g_events.back() == "flush");
}

TEST_CASE("client chat: a refusal other than a full pool drops the line and is logged") {
    Rig r;
    g_waiting = {"a", "b"};
    g_send_code = 0x12;
    r.flush();
    CHECK(g_waiting.empty());
    CHECK_FALSE(g_log.empty());
    CHECK(g_log[0].find("18") != std::string::npos);
}

TEST_CASE("client chat: nothing is sent without lines, and the flush always runs") {
    Rig r;
    r.flush();
    CHECK(g_sent.empty());
    CHECK(g_events == std::vector<std::string>{"flush"});
}

TEST_CASE("client chat: a marked line the client's own chat gets is taken, never added") {
    Rig r;
    r.flush();  // the flush hook learns the client's chat system
    CHECK(r.receive("#!emf1 S 0 emf/net@1 0/1 t hello", 0xABCD));
    REQUIRE(g_taken.size() == 1);
    CHECK(g_taken[0].first == "#!emf1 S 0 emf/net@1 0/1 t hello");
    CHECK(g_taken[0].second == 0xABCD);  // the speaker the line carries
    CHECK(g_added.empty());
}

TEST_CASE("client chat: ordinary lines, other chat systems and lines before the system is known go to the game") {
    Rig r;
    // before the first flush: the system is not known, so nothing is taken
    CHECK_FALSE(r.receive("#!emf1 S 0 emf/net@1 0/1 t early", 1));
    r.flush();
    CHECK_FALSE(r.receive("hello everyone", 1));
    std::vector<uint8_t> other(0x200, 0);  // another chat system (the server's, in a hosted game)
    CHECK_FALSE(r.receive("#!emf1 S 0 emf/net@1 0/1 t server", 1, other.data()));
    CHECK(g_added.size() == 3);
    CHECK(g_taken.empty());
}

TEST_CASE("client chat: a marked line the net service does not take is shown as chat") {
    Rig r;
    r.flush();
    g_take_marked = false;
    CHECK_FALSE(r.receive("#!emf1 S 0 emf/net@1 0/1 t x", 1));
    CHECK(g_added.size() == 1);
}
