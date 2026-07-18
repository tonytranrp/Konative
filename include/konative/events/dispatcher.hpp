#pragma once

#include <entt/entt.hpp>

// The ONE shared event-bus type for the whole project - not an event itself, just the generic
// machinery every event type in events/{lifecycle,window,input}/ is dispatched through.
//
// trigger<E>(...)  - immediate, synchronous dispatch (entt::dispatcher::trigger)
// enqueue<E>(...)  - deferred; delivered on the next update<E>()/update() call
// update()         - flush all queued event types once per frame (call from World::tick())
//
// Cross-thread posting does NOT enqueue directly onto this type - entt::dispatcher is not
// internally thread-safe (ARCHITECTURE.md \xc2\xa75). Worker threads post through a concurrentqueue
// MPMC queue instead; exactly one thread (the frame thread) drains that queue into
// Dispatcher::enqueue() before calling update().
namespace konative::events {

using Dispatcher = entt::dispatcher;

} // namespace konative::events
