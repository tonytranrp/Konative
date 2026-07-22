#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "konative/events/dispatcher.hpp"
#include "konative/scheduling/cross_thread_event_queue.hpp"

namespace {

// Test-local event, not part of the real events/ catalog - entt::dispatcher (and therefore this
// queue) works with any type, no registration step needed, so a test doesn't need a real
// framework event to exercise the boundary primitive itself.
struct CounterEvent {
    int value = 0;
};

// entt::dispatcher's sink<E>().connect<Candidate>(value_or_instance) binds a REFERENCE to
// value_or_instance, not a copy - it must be a real, named local variable with a stable address
// for the whole test (an inline temporary would dangle by the time dispatcher.update() actually
// invokes the connected slot). Bundles both outputs the concurrency test needs into one bound
// instance, matching the free-function-pointer-plus-one-bound-instance shape every connect<>() call
// in this codebase already uses (see test_events.cpp/test_app.cpp's own single-bool instances).
struct ConcurrencyTestSink {
    std::vector<bool>& seen;
    int& delivered_count;
};

} // namespace

TEST_CASE("CrossThreadEventQueue: post()+drain_into() from a single thread delivers every event, in order") {
    konative::scheduling::CrossThreadEventQueue<CounterEvent> queue;
    queue.post(CounterEvent{1});
    queue.post(CounterEvent{2});
    queue.post(CounterEvent{3});

    konative::events::Dispatcher dispatcher;
    std::vector<int> received;
    dispatcher.sink<CounterEvent>().connect<+[](std::vector<int>& out, const CounterEvent& e) {
        out.push_back(e.value);
    }>(received);

    queue.drain_into(dispatcher);
    CHECK(received.empty()); // drain_into() only enqueues - update() delivers, matching World::tick()'s own order
    dispatcher.update();

    REQUIRE(received.size() == 3);
    CHECK(received[0] == 1);
    CHECK(received[1] == 2);
    CHECK(received[2] == 3);
}

TEST_CASE("CrossThreadEventQueue: draining an empty queue is a safe no-op") {
    konative::scheduling::CrossThreadEventQueue<CounterEvent> queue;
    konative::events::Dispatcher dispatcher;
    int delivered = 0;
    dispatcher.sink<CounterEvent>().connect<+[](int& count, const CounterEvent&) { ++count; }>(delivered);

    queue.drain_into(dispatcher);
    dispatcher.update();

    CHECK(delivered == 0);
}

TEST_CASE("CrossThreadEventQueue: real concurrent producers, one consumer - every posted event survives "
          "the lock-free boundary with none lost, none duplicated") {
    // The actual reason this primitive exists (events/dispatcher.hpp's own doc comment,
    // scheduling/README.md's Hard Rule): entt::dispatcher is not thread-safe, so this is the one
    // real place worker-thread-originated events are allowed to cross into it. A single-threaded
    // test can't prove the lock-free queue itself is correct under real concurrent access - this
    // one actually launches real std::thread producers hammering post() at the same time.
    konative::scheduling::CrossThreadEventQueue<CounterEvent> queue;

    constexpr int kProducers = 8;
    constexpr int kEventsPerProducer = 5000;
    constexpr int kTotalEvents = kProducers * kEventsPerProducer;

    std::vector<std::thread> producers;
    producers.reserve(kProducers);
    for (int producer_id = 0; producer_id < kProducers; ++producer_id) {
        producers.emplace_back([&queue, producer_id] {
            for (int i = 0; i < kEventsPerProducer; ++i) {
                // Encode which producer sent it so a lost/duplicated event from any ONE producer
                // would show up as a wrong per-producer count, not just a wrong total (a stronger
                // check than total count alone - e.g. it would catch one producer's events being
                // silently dropped while another's are double-delivered, which a bare total-count
                // check could miss if the errors happened to cancel out).
                queue.post(CounterEvent{producer_id * kEventsPerProducer + i});
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    konative::events::Dispatcher dispatcher;
    std::vector<bool> seen(static_cast<std::size_t>(kTotalEvents), false);
    int delivered_count = 0;
    ConcurrencyTestSink sink{seen, delivered_count};
    dispatcher.sink<CounterEvent>().connect<+[](ConcurrencyTestSink& ctx, const CounterEvent& e) {
        ctx.seen[static_cast<std::size_t>(e.value)] = true;
        ++ctx.delivered_count;
    }>(sink);

    // All producers already joined above, so a single drain_into() after the fact is a valid use of
    // this class's "exactly one consumer thread" contract - draining WHILE producers are still
    // running would be the more realistic real-app shape, but proving lost-vs-not is cleaner to
    // assert against a quiesced queue where the exact expected total is known up front.
    queue.drain_into(dispatcher);
    dispatcher.update();

    CHECK(delivered_count == kTotalEvents);
    int missing = 0;
    for (bool was_seen : seen) {
        if (!was_seen) {
            ++missing;
        }
    }
    CHECK(missing == 0);
}
