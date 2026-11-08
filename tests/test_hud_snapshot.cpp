// The snapshot the HUD editor's UI hooks read without locks: published whole, freed only after a grace period.
#include <doctest.h>

#include <array>
#include <atomic>
#include <thread>

#include "core/hud_editor/snapshot.h"

using namespace emf::core::hud;

namespace {
struct Pair {
    uint64_t a = 0, b = 0;
    std::array<uint64_t, 32> pad{};
};
struct Counted {
    static inline int live = 0;
    uint64_t v = 0;
    Counted() { ++live; }
    Counted(const Counted& o) : v(o.v) { ++live; }
    Counted& operator=(const Counted&) = default;
    ~Counted() { --live; }
};
}  // namespace

TEST_CASE("SnapshotBox: current is the first snapshot, then the published one") {
    HudSnapshot first;
    first.serial = 7;
    SnapshotBox<HudSnapshot> box(first);
    REQUIRE(box.current() != nullptr);
    CHECK(box.current()->serial == 7);
    CHECK_FALSE(box.current()->active);
    HudSnapshot next;
    next.active = true;
    next.serial = 8;
    next.edits[2].scale = 1.5f;
    box.publish(next, 10);
    CHECK(box.current()->serial == 8);
    CHECK(box.current()->active);
    CHECK(box.current()->edits[2].scale == 1.5f);
    SnapshotBox<HudSnapshot> plain;
    CHECK(plain.current()->serial == 0);
}

TEST_CASE("SnapshotBox: an old snapshot stays readable until the grace has passed") {
    SnapshotBox<HudSnapshot> box;
    const HudSnapshot* old = box.current();
    HudSnapshot next;
    next.serial = 1;
    next.edits[0].x_percent = 5;
    box.publish(next, 1000);
    CHECK(box.current()->serial == 1);
    CHECK(old->serial == 0);  // still readable
    CHECK(box.retired() == 1);
    CHECK(box.collect(2999) == 1);
    CHECK(box.collect(3000) == 0);
    CHECK(box.retired() == 0);
}

TEST_CASE("SnapshotBox: publishing collects the snapshots whose grace has passed") {
    SnapshotBox<HudSnapshot> box;
    HudSnapshot s;
    s.serial = 1;
    box.publish(s, 0);
    s.serial = 2;
    box.publish(s, 1000);
    CHECK(box.retired() == 2);
    s.serial = 3;
    box.publish(s, 2500);  // the first retired at 0 is 2500 ms old: freed; the one from 1000 stays
    CHECK(box.retired() == 2);
    CHECK(box.collect(10000) == 0);
}

TEST_CASE("SnapshotBox: a reader thread never sees a torn snapshot while 20000 are published") {
    SnapshotBox<Pair> box;
    std::atomic<bool> done{false}, torn{false};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            const Pair* p = box.current();
            if (p->a != p->b) torn.store(true);
        }
    });
    for (uint64_t i = 1; i <= 20000; ++i) {
        Pair p;
        p.a = p.b = i;
        box.publish(p, 0);  // every retired snapshot is "published at 0", so none is freed before the grace
    }
    done.store(true, std::memory_order_release);
    reader.join();
    CHECK_FALSE(torn.load());
    CHECK(box.collect(SnapshotBox<Pair>::kGraceMs) == 0);
}

TEST_CASE("SnapshotBox: the destructor frees every snapshot") {
    Counted::live = 0;
    {
        SnapshotBox<Counted> box;
        Counted c;
        c.v = 1;
        box.publish(c, 0);
        box.publish(c, 0);
        box.publish(c, 0);
        CHECK(box.retired() == 3);
        CHECK(Counted::live >= 4);
    }
    CHECK(Counted::live == 0);
}
