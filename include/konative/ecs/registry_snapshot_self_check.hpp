#pragma once

#include <cstdint>
#include <sstream>

#include <cereal/archives/binary.hpp>
#include <entt/entt.hpp>

#include "konative/ecs/registry.hpp"

// A real, permanent self-check for EnTT's native snapshot API paired with cereal - the
// "historically-documented snapshot-API pairing" the dependency stack picked cereal for
// specifically, unused anywhere in this codebase until now (confirmed by repo-wide grep before
// writing this - the same "chosen but never wired" shape Taskflow/CrossThreadEventQueue had before
// this session's work). Same "code checks itself" precedent as
// scheduling/taskflow_self_check.hpp/events/next_event_awaiter_self_check.hpp.
namespace konative::ecs {

namespace detail {

// A test-local component, not part of any real gameplay module - this self-check only needs to
// prove the save/restore MECHANISM works, not exercise a real app component.
struct SnapshotSelfCheckComponent {
    std::uint64_t value = 0;
};

} // namespace detail
} // namespace konative::ecs

// cereal finds `serialize()` via ADL, so it must live in SnapshotSelfCheckComponent's own
// namespace (cereal's own documented lookup rule) - entt::snapshot's get<T>() calls archive(...)
// with the whole component by reference (not per-field), so a component with more than a
// pointer-sized POD payload needs an explicit serialize() like this one; cereal cannot
// auto-serialize an arbitrary user struct without it.
namespace konative::ecs::detail {

template <class Archive>
void serialize(Archive& archive, SnapshotSelfCheckComponent& component) {
    archive(component.value);
}

} // namespace konative::ecs::detail

namespace konative::ecs {

// Creates a registry with a few real entities+components, snapshots it into an in-memory binary
// buffer via entt::snapshot + cereal::BinaryOutputArchive, restores that buffer into a SECOND,
// fresh registry via entt::snapshot_loader + cereal::BinaryInputArchive (entt::snapshot_loader's
// own ENTT_ASSERT requires the destination registry to be empty - a second registry sidesteps any
// ambiguity about whether Registry::clear() alone satisfies that), then verifies every entity and
// its component value survived the round-trip exactly, entity-identity included (not just "5 in,
// 5 out" - a real bug shuffling identities across entities with different values would pass a
// count-only check but fail this one).
inline bool run_registry_snapshot_self_check() {
    constexpr int kEntityCount = 5;

    Registry source_registry;
    for (int i = 0; i < kEntityCount; ++i) {
        const auto entity = source_registry.create();
        source_registry.emplace<detail::SnapshotSelfCheckComponent>(
            entity, detail::SnapshotSelfCheckComponent{static_cast<std::uint64_t>(i) * 100U});
    }

    std::stringstream buffer;
    {
        cereal::BinaryOutputArchive output_archive(buffer);
        entt::snapshot{source_registry}
            .get<entt::entity>(output_archive)
            .get<detail::SnapshotSelfCheckComponent>(output_archive);
    }

    Registry destination_registry;
    {
        cereal::BinaryInputArchive input_archive(buffer);
        entt::snapshot_loader{destination_registry}
            .get<entt::entity>(input_archive)
            .get<detail::SnapshotSelfCheckComponent>(input_archive);
    }

    int restored_count = 0;
    for (auto [entity, component] : source_registry.view<detail::SnapshotSelfCheckComponent>().each()) {
        if (!destination_registry.valid(entity) ||
            !destination_registry.all_of<detail::SnapshotSelfCheckComponent>(entity)) {
            return false; // entity identity did not survive the round-trip
        }
        if (destination_registry.get<detail::SnapshotSelfCheckComponent>(entity).value != component.value) {
            return false; // entity survived, but its component value did not match
        }
        ++restored_count;
    }

    std::size_t destination_total = 0;
    for (auto [entity, component] : destination_registry.view<detail::SnapshotSelfCheckComponent>().each()) {
        (void)entity;
        (void)component;
        ++destination_total;
    }

    return restored_count == kEntityCount && destination_total == static_cast<std::size_t>(kEntityCount);
}

} // namespace konative::ecs
