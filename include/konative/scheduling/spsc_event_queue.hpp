#pragma once

#include <readerwriterqueue.h>

#include "konative/events/dispatcher.hpp"

// readerwriterqueue was fetched via CPM since the project's first dependency pass, alongside
// concurrentqueue, specifically as the SPSC-optimized sibling for the single-producer case
// (scheduling/README.md's own Hard Rule names both together) - but was never linked into any
// module's real usage nor #included anywhere (confirmed by repo-wide grep before writing this).
// Same "code checks itself" precedent as this codebase's other self-checks.
//
// STRICTLY single-producer, unlike CrossThreadEventQueue (cross_thread_event_queue.hpp), which
// wraps concurrentqueue's real MPMC support - moodycamel::ReaderWriterQueue's own documented
// contract requires exactly one thread calling try_enqueue() at a time (concurrent try_enqueue()
// calls from multiple threads are undefined behavior, not just slower). Use this only when a
// subsystem genuinely has one long-lived producer thread; use CrossThreadEventQueue for anything
// with more than one, or where producer threads come and go (e.g. a new thread per posted event,
// like KonativeAndroidApp::on_tick()'s periodic snapshot in jni_onload.cpp - that call site is
// genuinely MPMC-shaped, not SPSC, which is exactly why it uses CrossThreadEventQueue, not this).
namespace konative::scheduling {

template <typename Event>
class SpscEventQueue {
public:
    // Safe to call from exactly ONE producer thread - moodycamel::ReaderWriterQueue's own
    // documented contract, stricter than CrossThreadEventQueue::post()'s "any number of threads."
    //
    // enqueue(), NOT try_enqueue() - a real bug this project's own stress test caught: try_enqueue()
    // is explicitly documented as "does not allocate memory," bounded to the queue's current
    // capacity (default constructor's default is exactly 15), and silently returns false (dropping
    // the event, no exception, no error) once that capacity is exceeded. A test posting only a
    // handful of events would never have revealed this; a real 20000-event single-producer stress
    // test found only 15 survived. enqueue() allocates an additional block automatically when
    // needed (never silently drops - it either succeeds or fails loudly via bad_alloc/abort), giving
    // this class the same "caller never worries about capacity" contract CrossThreadEventQueue's own
    // concurrentqueue-backed post() already has.
    void post(Event event) { queue_.enqueue(std::move(event)); }

    // Same drain_into() contract as CrossThreadEventQueue: does NOT call dispatcher.update() itself
    // (the caller decides when to flush that frame); safe to call from exactly one consumer thread.
    void drain_into(konative::events::Dispatcher& dispatcher) {
        Event event;
        while (queue_.try_dequeue(event)) {
            dispatcher.enqueue<Event>(std::move(event));
        }
    }

    // For tests/diagnostics only, same caveat as CrossThreadEventQueue's own size_approx().
    [[nodiscard]] std::size_t size_approx() const { return queue_.size_approx(); }

private:
    moodycamel::ReaderWriterQueue<Event> queue_;
};

} // namespace konative::scheduling
