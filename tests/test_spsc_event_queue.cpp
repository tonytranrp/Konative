#include <vector>

#include <doctest/doctest.h>

#include "konative/events/dispatcher.hpp"
#include "konative/scheduling/spsc_event_queue.hpp"
#include "konative/scheduling/spsc_event_queue_self_check.hpp"

namespace {

// Test-local event, not part of the real events/ catalog - same rationale as
// test_cross_thread_event_queue.cpp's own CounterEvent (entt::dispatcher works with any type, no
// registration step needed).
struct CounterEvent {
    int value = 0;
};

} // namespace

TEST_CASE("run_spsc_event_queue_self_check: a real single producer thread delivers every event, losslessly AND in order") {
    CHECK(konative::scheduling::run_spsc_event_queue_self_check());
}

TEST_CASE("SpscEventQueue: post()+drain_into() from a single thread delivers every event, in order") {
    konative::scheduling::SpscEventQueue<CounterEvent> queue;
    queue.post(CounterEvent{1});
    queue.post(CounterEvent{2});
    queue.post(CounterEvent{3});

    konative::events::Dispatcher dispatcher;
    std::vector<int> received;
    dispatcher.sink<CounterEvent>().connect<+[](std::vector<int>& out, const CounterEvent& e) {
        out.push_back(e.value);
    }>(received);

    queue.drain_into(dispatcher);
    CHECK(received.empty()); // drain_into() only enqueues - update() delivers
    dispatcher.update();

    REQUIRE(received.size() == 3);
    CHECK(received[0] == 1);
    CHECK(received[1] == 2);
    CHECK(received[2] == 3);
}

TEST_CASE("SpscEventQueue: draining an empty queue is a safe no-op") {
    konative::scheduling::SpscEventQueue<CounterEvent> queue;
    konative::events::Dispatcher dispatcher;
    int delivered = 0;
    dispatcher.sink<CounterEvent>().connect<+[](int& count, const CounterEvent&) { ++count; }>(delivered);

    queue.drain_into(dispatcher);
    dispatcher.update();

    CHECK(delivered == 0);
}
