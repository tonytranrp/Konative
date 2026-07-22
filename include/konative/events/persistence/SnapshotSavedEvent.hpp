#pragma once

#include <cstddef>

namespace konative::events::persistence {

// Fired from a background thread (via konative::scheduling::CrossThreadEventQueue<SnapshotSavedEvent>
// - entt::dispatcher itself is not thread-safe, ARCHITECTURE.md section 5) once a real, off-main-
// thread unit of work finishes on an ECS registry snapshot that was itself captured synchronously on
// the main thread beforehand (entt::snapshot reads live storage directly - capturing it while the
// main thread might concurrently mutate the same registry would be a real data race; the bytes this
// event's producer hands to the background thread are already-serialized and immutable by the time
// the thread touches them, so there is no race, just a real serialized-bytes handoff).
struct SnapshotSavedEvent {
    std::size_t byte_size = 0;
};

} // namespace konative::events::persistence
