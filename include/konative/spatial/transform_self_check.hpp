#pragma once

#include <unordered_map>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include "konative/ecs/registry.hpp"
#include "konative/spatial/transform.hpp"

// A real, permanent regression guard for spatial::Transform/to_matrix() - the first genuinely
// exercised use of GLM's quaternion/matrix_transform GTC extensions anywhere in this codebase (the
// existing ecs/glm_storage_self_check.hpp only ever tested plain vec3 storage, never a quaternion
// or a real matrix composition). Same "code checks itself" precedent as every other self-check in
// this codebase.
namespace konative::spatial {

namespace detail {

constexpr float kEpsilon = 0.0001F;

inline bool nearly_equal(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEpsilon));
}

inline bool nearly_equal(const glm::vec4& a, const glm::vec4& b) {
    return glm::all(glm::epsilonEqual(a, b, kEpsilon));
}

} // namespace detail

// Verifies, in order: (1) a real Transform (position/rotation/scale together, not just one field)
// round-trips correctly through EnTT's paged component storage, the same real property
// glm_storage_self_check.hpp already proved for a bare vec3 - now proved for the actual component
// this dependency was chosen for; (2) to_matrix() is mathematically correct for translation, scale,
// and rotation individually, AND composed together, against real, hand-computed expected results.
// "It compiles and returns some mat4" would not catch a wrong composition order (e.g. S*R*T instead
// of the correct T*R*S) or a sign/axis error in the identity-quaternion constant - both are real,
// plausible mistakes this check is specifically designed to catch.
inline bool run_transform_self_check() {
    // (1) Real round-trip through EnTT's paged component storage.
    {
        constexpr int kEntityCount = 8;
        konative::ecs::Registry registry;
        std::unordered_map<entt::entity, float> expected_base_values;

        // A real, non-identity, non-trivial rotation (45 degrees around Y) - proves the whole
        // struct (not just position/scale, which are structurally identical to vec3 already
        // proven by glm_storage_self_check.hpp) survives storage, including the one NEW field
        // type (glm::quat) this check specifically exists to cover.
        const glm::quat non_trivial_rotation =
            glm::angleAxis(glm::radians(45.0F), glm::vec3{0.0F, 1.0F, 0.0F});

        for (int i = 0; i < kEntityCount; ++i) {
            const auto entity = registry.create();
            const float f = static_cast<float>(i);
            expected_base_values[entity] = f;
            Transform transform{};
            transform.position = glm::vec3{f, f * 2.0F, f * 3.0F};
            transform.rotation = non_trivial_rotation;
            transform.scale = glm::vec3{f + 1.0F, f + 1.0F, f + 1.0F};
            registry.emplace<Transform>(entity, transform);
        }

        int checked = 0;
        for (auto [entity, transform] : registry.view<Transform>().each()) {
            const float f = expected_base_values.at(entity);
            if (!detail::nearly_equal(transform.position, glm::vec3{f, f * 2.0F, f * 3.0F})) {
                return false; // position didn't round-trip correctly through EnTT's storage
            }
            if (!detail::nearly_equal(transform.scale,
                                       glm::vec3{f + 1.0F, f + 1.0F, f + 1.0F})) {
                return false; // scale didn't round-trip correctly through EnTT's storage
            }
            if (transform.rotation.w != non_trivial_rotation.w ||
                transform.rotation.x != non_trivial_rotation.x ||
                transform.rotation.y != non_trivial_rotation.y ||
                transform.rotation.z != non_trivial_rotation.z) {
                return false; // rotation (the one new field type) didn't round-trip exactly
            }
            ++checked;
        }
        if (checked != kEntityCount) {
            return false;
        }
    }

    // (2) to_matrix() mathematical correctness.
    {
        // Identity transform -> identity matrix, exactly (no epsilon needed - every component is
        // an exact literal, no trigonometry involved).
        if (to_matrix(Transform{}) != glm::mat4(1.0F)) {
            return false;
        }

        // Pure translation: applying the matrix to the origin must yield exactly `position`.
        {
            Transform t{};
            t.position = glm::vec3{5.0F, 3.0F, -2.0F};
            const glm::vec4 result = to_matrix(t) * glm::vec4{0.0F, 0.0F, 0.0F, 1.0F};
            if (!detail::nearly_equal(result, glm::vec4{5.0F, 3.0F, -2.0F, 1.0F})) {
                return false;
            }
        }

        // Pure scale: applying the matrix to (1,1,1) must yield exactly `scale`.
        {
            Transform t{};
            t.scale = glm::vec3{2.0F, 3.0F, 4.0F};
            const glm::vec4 result = to_matrix(t) * glm::vec4{1.0F, 1.0F, 1.0F, 1.0F};
            if (!detail::nearly_equal(result, glm::vec4{2.0F, 3.0F, 4.0F, 1.0F})) {
                return false;
            }
        }

        // Pure rotation: a real 90-degree rotation around +Y takes +X to -Z. Verified against the
        // real right-handed rotation-around-Y matrix by hand before writing this
        // (R_y(90) = [[0,0,1],[0,1,0],[-1,0,0]], applied to (1,0,0) = (0,0,-1)) - GLM defaults to
        // right-handed coordinates (matching OpenGL/GLSL convention), confirmed rather than
        // assumed.
        {
            Transform t{};
            t.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3{0.0F, 1.0F, 0.0F});
            const glm::vec4 result = to_matrix(t) * glm::vec4{1.0F, 0.0F, 0.0F, 1.0F};
            if (!detail::nearly_equal(result, glm::vec4{0.0F, 0.0F, -1.0F, 1.0F})) {
                return false;
            }
        }

        // Composed: verifies the actual T * R * S composition ORDER, not just each piece in
        // isolation (a wrong order, e.g. S*R*T, would still pass all three checks above but fail
        // this one). A point at (1,0,0), scaled 2x, rotated 90 degrees around +Y (taking +X to
        // -Z), then translated by (10,0,0), must land at exactly (10,0,-2).
        {
            Transform t{};
            t.position = glm::vec3{10.0F, 0.0F, 0.0F};
            t.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3{0.0F, 1.0F, 0.0F});
            t.scale = glm::vec3{2.0F, 2.0F, 2.0F};
            const glm::vec4 result = to_matrix(t) * glm::vec4{1.0F, 0.0F, 0.0F, 1.0F};
            if (!detail::nearly_equal(result, glm::vec4{10.0F, 0.0F, -2.0F, 1.0F})) {
                return false;
            }
        }
    }

    return true;
}

} // namespace konative::spatial
