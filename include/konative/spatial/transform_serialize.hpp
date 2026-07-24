#pragma once

#include "konative/spatial/transform.hpp"

// cereal serialization for spatial::Transform - a separate opt-in header, NOT folded into
// transform.hpp, so Transform consumers that never snapshot (the common case for pure math/render
// code) don't pull cereal's headers into their include graph just to use the component.
//
// Serializes field-by-field through each glm member's own .x/.y/.z/.w floats rather than defining
// serialize() overloads for glm::vec3/glm::quat themselves - deliberately: cereal's non-intrusive
// pattern would require those overloads to live in namespace glm for ADL to find them, and a
// framework header quietly injecting serialize() into a third-party library's namespace is a real
// collision hazard for any downstream project that already defines its own glm-cereal adapters
// (a widely-copied snippet). Scoping everything to konative::spatial keeps this file's footprint
// exactly one type: the one it owns.
//
// cereal finds this via ADL (same documented lookup rule registry_snapshot_self_check.hpp's own
// SnapshotSelfCheckComponent::serialize() cites) - it must live in Transform's own namespace.
namespace konative::spatial {

template <class Archive>
void serialize(Archive& archive, Transform& transform) {
    archive(transform.position.x, transform.position.y, transform.position.z,
            transform.rotation.w, transform.rotation.x, transform.rotation.y,
            transform.rotation.z, transform.scale.x, transform.scale.y, transform.scale.z);
}

} // namespace konative::spatial
