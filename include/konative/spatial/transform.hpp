#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// Real ECS-side Transform component - ARCHITECTURE.md section 4's own "GLM for ECS-side
// transforms/components" framing (the dependency table's actual stated reason GLM was chosen in
// the first place), unresolved until now: confirmed via repo-wide grep that no real, reusable
// Transform-shaped component existed anywhere - ecs/glm_storage_self_check.hpp's own
// PackedTransformSelfCheckComponent is a synthetic, self-check-only stand-in (proving GLM's packed
// vec3 round-trips through EnTT storage), never meant to be a real component other code emplaces.
//
// Position/rotation/scale via glm::vec3/glm::quat/glm::vec3 - the standard, conventional ECS
// Transform layout (Unity, Godot, Bevy all use exactly this shape), not a narrower 2D-only one
// (vec2 position, float rotation), even though this project's only current rendering surface
// (JVM-hosted Compose, ARCHITECTURE.md section 6.2) is 2D. Nothing in this codebase yet drives a
// specific 2D-vs-3D choice - no C++-side rendering/physics consumer exists at all yet - so the
// least-regret, most broadly useful shape wins over guessing at a narrower one 2D usage can still
// express fully (z becomes depth/layering, rotation constrained to the Z axis if needed).
//
// Deliberately NO hierarchy (no parent entity/component, no local-vs-world distinction) - matching
// this project's own established "no real driving need, don't build it speculatively" discipline
// (the same reasoning ecs/registry.hpp's own kNullEntity was left unwired for). Hierarchy is a
// real, separate design decision (world-space caching, dirty-flag propagation, cycle prevention)
// worth making when a real consumer actually needs nested transforms, not preemptively.
namespace konative::spatial {

struct Transform {
    glm::vec3 position{0.0F, 0.0F, 0.0F};
    // w, x, y, z - identity. Explicit, not glm::quat{}'s own default constructor: confirmed
    // against the real vendored glm/detail/type_quat.hpp that qua()'s default constructor is
    // GLM_DEFAULT (compiler-generated, `= default`), which leaves its 4 floats genuinely
    // uninitialized unless GLM_FORCE_CTOR_INIT is defined (this project doesn't define it
    // anywhere) - an uninitialized "identity" rotation would be a real, silent bug, not a style
    // preference. Constructor argument order (w, x, y, z) also confirmed against that same file's
    // real `qua(T w, T x, T y, T z)` declaration, not assumed.
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F, 1.0F, 1.0F};
};

// The one real, broadly useful operation every Transform consumer needs: compose position/
// rotation/scale into a real 4x4 model matrix (local-to-parent, or local-to-world since this
// component has no hierarchy - see the struct's own comment above). Standard T * R * S
// composition order (scale applied first/innermost, matching every real-world renderer's and
// physics engine's own convention - GLM itself is column-major/right-multiply, so this reads
// right-to-left: a point is scaled, then rotated, then translated). A free function, not a member,
// matching this codebase's established convention of keeping ECS components plain data
// (HeartbeatCounter, AppConfig) with behavior expressed separately. Verified against real,
// hand-computed expected results (not just "it compiles and returns a mat4") by
// transform_self_check.hpp.
inline glm::mat4 to_matrix(const Transform& transform) {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0F), transform.position);
    const glm::mat4 rotation = glm::mat4_cast(transform.rotation);
    const glm::mat4 scale = glm::scale(glm::mat4(1.0F), transform.scale);
    return translation * rotation * scale;
}

} // namespace konative::spatial
