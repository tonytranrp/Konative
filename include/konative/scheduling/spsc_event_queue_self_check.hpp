#pragma once

#include <cstddef>
#include <thread>
#include <vector>

#include "konative/events/dispatcher.hpp"
#include "konative/scheduling/spsc_event_queue.hpp"

// A real, permanent self-check for SpscEventQueue (moodycamel::ReaderWriterQueue) - same
// "code checks itself" precedent as this codebase's other self-checks. Spins up a real single
// producer thread (SpscEventQueue's own documented single-producer contract, unlike
// CrossThreadEventQueue's MPMC support) posting a real, deliberately-large number of events - large
// enough to exceed ReaderWriterQueue's small default initial capacity, which is exactly the
// real bug this self-check's own desktop test counterpart (tests/test_spsc_event_queue.cpp) caught
// during development: post() originally used try_enqueue() ("does not allocate memory" per its own
// doc comment), silently dropping every event past the default capacity of 15. Verifies both
// losslessness AND exact FIFO order - the one guarantee this SPSC-optimized queue makes that the
// MPMC CrossThreadEventQueue doesn't (no cross-producer ordering there, since there can be more
// than one producer).
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

} // namespace konative::scheduling
