#pragma once

#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

#include "konative/events/dispatcher.hpp"
#include "konative/scheduling/spsc_event_queue.hpp"

// Two real, permanent self-checks for SpscEventQueue (moodycamel::ReaderWriterQueue) - same
// "code checks itself" precedent as this codebase's other self-checks.
//
// run_spsc_event_queue_self_check() spins up a real single producer thread (SpscEventQueue's own
// documented single-producer contract, unlike CrossThreadEventQueue's MPMC support), fully joins
// it, THEN drains - posting a real, deliberately-large number of events, large enough to exceed
// ReaderWriterQueue's small default initial capacity, which is exactly the real bug this check's
// own desktop test counterpart (tests/test_spsc_event_queue.cpp) caught during development: post()
// originally used try_enqueue() ("does not allocate memory" per its own doc comment), silently
// dropping every event past the default capacity of 15. Verifies both losslessness AND exact FIFO
// order - the one guarantee this SPSC-optimized queue makes that the MPMC CrossThreadEventQueue
// doesn't (no cross-producer ordering there, since there can be more than one producer).
//
// run_spsc_event_queue_concurrent_self_check() proves a DIFFERENT property the check above can't:
// the producer-joined-before-any-drain shape above only ever exercises a fully-serialized
// post-then-drain sequence, never post() and drain_into() actually running concurrently on two
// threads at once - the real shape this class's own doc comment describes as its intended use (a
// long-lived producer thread). This one keeps the producer running and drains in a loop from the
// calling thread while it's still posting - found via a real code-review pass (2026-07-22) noting
// the first check's claimed coverage was broader than what it actually exercised, the same shape of
// gap NextEventAwaiter's empty-struct-vs-real-value self-check had.
namespace konative::scheduling {

namespace detail {
struct SpscSelfCheckEvent {
    int value = 0;
};
} // namespace detail

inline bool run_spsc_event_queue_self_check(int event_count = 20000) {
    SpscEventQueue<detail::SpscSelfCheckEvent> queue;

    std::thread producer([&queue, event_count] {
        for (int i = 0; i < event_count; ++i) {
            queue.post(detail::SpscSelfCheckEvent{i});
        }
    });
    producer.join();

    konative::events::Dispatcher dispatcher;
    std::vector<int> received;
    received.reserve(static_cast<std::size_t>(event_count));
    dispatcher.sink<detail::SpscSelfCheckEvent>()
        .connect<+[](std::vector<int>& out, const detail::SpscSelfCheckEvent& event) {
            out.push_back(event.value);
        }>(received);

    queue.drain_into(dispatcher);
    dispatcher.update();

    if (received.size() != static_cast<std::size_t>(event_count)) {
        return false; // lost (or somehow gained) events
    }
    for (int i = 0; i < event_count; ++i) {
        if (received[static_cast<std::size_t>(i)] != i) {
            return false; // delivered, but out of the real FIFO order this queue promises
        }
    }
    return true;
}

// Sibling to the check above, proving a DIFFERENT property: that above only ever proves
// losslessness/ordering for a fully-serialized post-then-join-then-drain sequence (the producer
// thread is joined, past tense, before drain_into() is ever called) - it never actually exercises
// post() and drain_into() running concurrently on two different threads at once, which is this
// class's own documented real use case (spsc_event_queue.hpp's own doc comment: "a subsystem
// genuinely has one long-lived producer thread"). This check keeps the producer thread running
// and calls drain_into() in a loop from the calling thread WHILE it's still posting, the actual
// concurrent-overlap shape a real long-lived producer would create. Defaults to a much larger event
// count than the sequential check above - a small count risks the producer finishing (thread
// creation alone costs real, if small, OS-scheduling time) before the consumer's loop gets a
// meaningful number of iterations in, which would make this check accidentally degenerate back into
// the sequential shape it exists to NOT be; a much larger count makes genuine overlap the
// overwhelmingly likely case on any real machine, not just possible. Bounded by a wall-clock
// deadline, not an iteration count, so a genuine regression here fails this check within a few
// seconds instead of hanging the process indefinitely.
inline bool run_spsc_event_queue_concurrent_self_check(int event_count = 1000000) {
    SpscEventQueue<detail::SpscSelfCheckEvent> queue;

    std::thread producer([&queue, event_count] {
        for (int i = 0; i < event_count; ++i) {
            queue.post(detail::SpscSelfCheckEvent{i});
        }
    });

    konative::events::Dispatcher dispatcher;
    std::vector<int> received;
    received.reserve(static_cast<std::size_t>(event_count));
    dispatcher.sink<detail::SpscSelfCheckEvent>()
        .connect<+[](std::vector<int>& out, const detail::SpscSelfCheckEvent& event) {
            out.push_back(event.value);
        }>(received);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool timed_out = false;
    while (received.size() < static_cast<std::size_t>(event_count)) {
        queue.drain_into(dispatcher);
        dispatcher.update();
        if (received.size() < static_cast<std::size_t>(event_count) &&
            std::chrono::steady_clock::now() > deadline) {
            timed_out = true;
            break;
        }
    }
    producer.join(); // must join before returning either way, or ~thread() calls std::terminate
    if (timed_out) {
        return false; // a real concurrent-drain regression, or a producer that never finished
    }

    // One final drain in case the last few events were posted after the loop's last iteration
    // but before join() observed the producer thread's completion.
    queue.drain_into(dispatcher);
    dispatcher.update();

    if (received.size() != static_cast<std::size_t>(event_count)) {
        return false; // lost (or somehow gained) events under genuine concurrent overlap
    }
    for (int i = 0; i < event_count; ++i) {
        if (received[static_cast<std::size_t>(i)] != i) {
            return false; // delivered, but out of the real FIFO order this queue promises
        }
    }
    return true;
}

} // namespace konative::scheduling
