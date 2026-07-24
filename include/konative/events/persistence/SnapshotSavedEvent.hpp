#pragma once

#include <cstddef>

#include "konative/core/type_traits.hpp"

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
    // Whether the serialized bytes were also durably written to the on-device state file
    // (ecs/snapshot_file.hpp's atomic temp-file-then-rename path) - false both when the write
    // genuinely failed AND when no state-file path was configured at all (the producer logs which
    // of those it was; this event only carries the outcome). Until this field existed the event's
    // "Saved" name was honestly aspirational - the bytes were serialized, counted, and dropped.
    bool persisted_to_disk = false;
};

static_assert(konative::core::EventType<SnapshotSavedEvent>);

} // namespace konative::events::persistence
