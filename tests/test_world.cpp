#include <vector>

#include <doctest/doctest.h>

#include "konative/ecs/world.hpp"

// World (the Registry + SystemSequence + Dispatcher composition root) has been landed and verified
// on real hardware since 2026-07-21 (ARCHITECTURE.md section 6.7's own status table), but - unlike
// every other capability landed alongside it (Taskflow, BS::thread_pool, GLM+EnTT storage, EnTT
// snapshot+cereal, each with both an Android self-check AND a matching permanent desktop test) - had
// zero desktop unit test coverage of its own: nothing exercised SystemSequence::add()/World::tick()'s
// real ordering contract, or Registry+View+System used together, outside jni_onload.cpp's on-device
// proof. Closes that asymmetry - pure test-authoring against an already-shipped, already-stable API,
// no production code changes.

namespace {

struct Counter {
    int value = 0;
};

struct TickCompletedEvent {};

void increment_counters(konative::ecs::Registry& registry, float /*delta_seconds*/) {
    for (auto [entity, counter] : registry.view<Counter>().each()) {
        ++counter.value;
    }
}

} // namespace

TEST_CASE("World::tick(): systems run in registration order, every tick") {
    konative::ecs::World world;
    std::vector<int> execution_order;

    world.systems().add([&execution_order](konative::ecs::Registry&, float) {
        execution_order.push_back(1);
    });
    world.systems().add([&execution_order](konative::ecs::Registry&, float) {
        execution_order.push_back(2);
    });

    world.tick(0.016F);
    REQUIRE(execution_order.size() == 2);
    CHECK(execution_order[0] == 1);
    CHECK(execution_order[1] == 2);

    world.tick(0.016F);
    REQUIRE(execution_order.size() == 4);
    CHECK(execution_order[2] == 1);
    CHECK(execution_order[3] == 2);
}

TEST_CASE("World::tick(): a real system mutating a real component via Registry::view is visible after N ticks") {
    konative::ecs::World world;
    constexpr int kEntityCount = 4;
    for (int i = 0; i < kEntityCount; ++i) {
        world.registry().emplace<Counter>(world.registry().create());
    }
    world.systems().add(&increment_counters);

    constexpr int kTickCount = 10;
    for (int i = 0; i < kTickCount; ++i) {
        world.tick(0.016F);
    }

    int combined = 0;
    int entity_count = 0;
    for (auto [entity, counter] : world.registry().view<Counter>().each()) {
        combined += counter.value;
        ++entity_count;
    }
    CHECK(entity_count == kEntityCount);
    CHECK(combined == kEntityCount * kTickCount); // every entity incremented once per tick, every tick
}

TEST_CASE("World::tick(): Dispatcher::update() flushes an event enqueued by THIS tick's own systems, not before them") {
    konative::ecs::World world;
    bool received = false;
    world.events().sink<TickCompletedEvent>().connect<+[](bool& flag, const TickCompletedEvent&) {
        flag = true;
    }>(received);

    world.systems().add([&world](konative::ecs::Registry&, float) {
        world.events().enqueue<TickCompletedEvent>(TickCompletedEvent{});
    });

    CHECK_FALSE(received);
    world.tick(0.016F);
    // If Dispatcher::update() ran before systems_.run() (the wrong order), this event would still be
    // sitting unflushed and `received` would be false here - it wouldn't be delivered until a SECOND
    // tick(). ecs/README.md's own Hard Rule ("World::tick() is the one place Dispatcher::update()
    // gets called") specifically requires systems-then-update, within the same tick.
    CHECK(received);
}
