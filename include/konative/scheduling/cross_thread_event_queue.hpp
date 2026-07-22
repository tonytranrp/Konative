#pragma once

#include <concurrentqueue.h>

#include "konative/events/dispatcher.hpp"

// The lock-free MPMC boundary events::Dispatcher's own doc comment and this module's README (Hard
// Rules) both require: entt::dispatcher is not internally thread-safe, so any worker thread wanting
// to post an event must go through here instead of calling Dispatcher::enqueue()/trigger()
// directly. One CrossThreadEventQueue<Event> instance per event TYPE, mirroring
// entt::dispatcher's own per-type sink model - a real system posting several distinct event types
// across threads owns one queue per type, not one polymorphic any-event queue, so drain_into() stays
// a real, typed dispatcher.enqueue<Event>() call with no type erasure anywhere in the boundary.
namespace konative::scheduling {

template <typename Event>
class CrossThreadEventQueue {
public:
    // Safe to call from ANY thread, any number of threads concurrently, including the frame thread
    // itself - moodycamel::ConcurrentQueue's own documented guarantee (concurrentqueue.h's top
    // comment: "any number of threads can call the enqueue methods... any number of threads can
    // call try_dequeue"). Never blocks.
    void post(Event event) { queue_.enqueue(std::move(event)); }

    // Drains everything currently queued into dispatcher.enqueue<Event>() - deliberately does NOT
    // call dispatcher.update() itself, since events/dispatcher.hpp's own documented update()
    // contract ("flush all queued event types once per frame") means the CALLER decides when in its
    // own frame to flush, not this class. World::tick() itself has no built-in cross-thread-queue
    // support (deliberately not added speculatively - a real, generic `World::register_cross_thread
    // _queue<Event>()` mechanism is a bigger abstraction than this module needed for its one real
    // consumer so far, jni_onload.cpp's KonativeAndroidApp; add it if/when a second real consumer
    // justifies it, per this project's own "don't build abstractions before a real need" rule).
    // KonativeAndroidApp::on_tick() drains AFTER World::tick() already ran Dispatcher::update() for
    // that frame, so a drained event is enqueued but not flushed until the NEXT frame's update() -
    // one frame of latency, harmless for an inherently-asynchronous cross-thread notification. Safe
    // to call from exactly ONE thread at a time - this class doesn't itself enforce single-consumer,
    // that's the caller's responsibility per this module's own Hard Rule ("exactly one thread, the
    // frame thread, drains").
    void drain_into(konative::events::Dispatcher& dispatcher) {
        Event event;
        while (queue_.try_dequeue(event)) {
            dispatcher.enqueue<Event>(std::move(event));
        }
    }

    // For tests/diagnostics only - moodycamel::ConcurrentQueue documents size_approx() as exactly
    // that, an approximation safe to call concurrently with enqueue/dequeue, never used here to
    // gate correctness (e.g. never "if size_approx() > 0" before draining - drain_into() above
    // already handles an empty queue correctly via try_dequeue's own return value).
    [[nodiscard]] std::size_t size_approx() const { return queue_.size_approx(); }

private:
    moodycamel::ConcurrentQueue<Event> queue_;
};

} // namespace konative::scheduling
