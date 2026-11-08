#include <doctest.h>
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "common/perf_book.h"
#include "core/dispatcher.h"

using emf::PerfBook;
using emf::perf_lines;
using emf::core::Dispatcher;

TEST_CASE("perf: the book adds up each name's steps, keeps the worst, and is empty again after take") {
    PerfBook book;
    book.add("lua (server)", 2.0);
    book.add("lua (server)", 6.0);
    book.add("raids (server)", 0.5);
    auto rows = book.take();
    REQUIRE(rows.size() == 2u);
    CHECK(rows[0].name == "lua (server)");  // the largest total first
    CHECK(rows[0].steps == 2u);
    CHECK(rows[0].total_ms == doctest::Approx(8.0));
    CHECK(rows[0].max_ms == doctest::Approx(6.0));
    CHECK(rows[1].name == "raids (server)");
    CHECK(book.take().empty());
}

TEST_CASE("perf: report lines give the average per step, the steps, the worst step and the share of the time") {
    PerfBook book;
    for (int i = 0; i < 600; ++i) book.add("game: server world step", i == 7 ? 21.0 : 3.0);
    book.add("beastmaster.lua (server)", 12.0);
    const auto lines = perf_lines(book.take(), 10.0);
    REQUIRE(lines.size() == 2u);
    CHECK(lines[0] == "game: server world step: 3.03 ms a step (600 steps), worst 21.0 ms, 18.2% of the time");
    CHECK(lines[1] == "beastmaster.lua (server): 12.00 ms a step (1 steps), worst 12.0 ms, 0.1% of the time");
}

namespace {
double g_now = 0;
double fake_clock() { return g_now; }
void takes_2ms(EmfWorld*, void*) { g_now += 2.0; }
void takes_30ms(EmfWorld*, void*) { g_now += 30.0; }
EmfWorld* const kWorld = reinterpret_cast<EmfWorld*>(0x2000);

std::string this_module() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    s = s.substr(s.find_last_of(L"\\/") + 1);
    std::string out;
    for (wchar_t c : s) out += static_cast<char>(c);  // the test program's name is ASCII
    return out;
}
}  // namespace

TEST_CASE("dispatcher perf: a module's tick and posted work in a step go to the book once, under its name and world; "
          "a call over 25 ms is reported") {
    g_now = 0;
    Dispatcher d;
    PerfBook book;
    std::vector<std::pair<std::string, double>> slow;
    d.set_perf(&book, [&](const std::string& who, double ms) { slow.emplace_back(who, ms); }, fake_clock);
    d.subscribe_tick(takes_2ms, nullptr);
    d.subscribe_tick(takes_30ms, nullptr);
    d.post(EMF_WORLD_SERVER, takes_2ms, nullptr);
    d.run(kWorld, EMF_WORLD_SERVER, [](const std::string&) {});
    d.run(kWorld, EMF_WORLD_SERVER, [](const std::string&) {});
    d.run(kWorld, EMF_WORLD_CLIENT, [](const std::string&) {});
    const auto rows = book.take();
    REQUIRE(rows.size() == 2u);
    const std::string me = this_module();
    CHECK(rows[0].name == me + " (server)");  // all three callbacks live in this test program
    CHECK(rows[0].steps == 2u);
    CHECK(rows[0].total_ms == doctest::Approx(34.0 + 32.0));  // the posted work ran in the first step only
    CHECK(rows[0].max_ms == doctest::Approx(34.0));
    CHECK(rows[1].name == me + " (client)");
    CHECK(rows[1].total_ms == doctest::Approx(32.0));
    REQUIRE(slow.size() == 3u);
    CHECK(slow[0] == std::pair<std::string, double>(me, 30.0));
}

TEST_CASE("dispatcher perf: without a book nothing is timed and every callback still runs") {
    g_now = 0;
    Dispatcher d;
    d.subscribe_tick(takes_2ms, nullptr);
    d.run(kWorld, EMF_WORLD_SERVER, [](const std::string&) {});
    CHECK(g_now == doctest::Approx(2.0));
}
