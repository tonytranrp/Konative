#pragma once

#include <unordered_map>

#include <glm/glm.hpp>

#include "konative/ecs/registry.hpp"

// Verifies EnTT's paged component storage correctly preserves GLM's PACKED vec3 layout -
// ARCHITECTURE.md section 4's own actual default recommendation ("default to GLM's packed
// (non-SIMD-aligned) types for EnTT component storage"), unresolved until now (confirmed zero real
// usage anywhere via repo-wide grep before this - GLM was fetched via CPM since the project's first
// dependency pass but never linked into any module nor included by any real code). Same
// "code checks itself" precedent as the other self-checks in this codebase.
//
// Deliberately does NOT also exercise GLM's ALIGNED types (glm::aligned_vec3 etc.), despite
// ARCHITECTURE.md section 4 flagging that combination too ("until the aligned-type + EnTT-paged-
// storage combination is verified in CI") - a real attempt at this found aligned gentypes need more
// than just `#define GLM_FORCE_ALIGNED_GENTYPES`: on Clang/GCC (unlike MSVC), GLM's own
// glm/detail/setup.hpp gates them behind `GLM_ARCH & GLM_ARCH_SIMD_BIT` - real detected/enabled SIMD
// instruction-set support (e.g. -msse2/-mavx on x86_64, different NEON flags on arm64) - which this
// project's build does not currently enable anywhere, for any target. Force-enabling SIMD codegen
// (globally, or even scoped to just this header) is a real, broader, platform-specific compiler-flag
// decision with its own portability tradeoffs (a baseline instruction-set requirement across every
// x86_64 build, a DIFFERENT flag entirely needed for Android arm64/NEON) - genuinely out of scope for
// this one spike to decide unilaterally. The aligned-type half of ARCHITECTURE.md's own flagged
// question stays open, honestly, rather than being forced through with a narrow hack; revisit if/
// when a real SIMD-performance need justifies that broader decision.
namespace konative::ecs {

namespace detail {

struct PackedTransformSelfCheckComponent {
    glm::vec3 position{0.0F, 0.0F, 0.0F};
};

} // namespace detail

inline bool run_glm_ecs_storage_self_check() {
    constexpr int kEntityCount = 8;
    Registry registry;
    std::unordered_map<entt::entity, float> expected_base_values;

    for (int i = 0; i < kEntityCount; ++i) {
        const auto entity = registry.create();
        const float f = static_cast<float>(i);
        expected_base_values[entity] = f;
        registry.emplace<detail::PackedTransformSelfCheckComponent>(
            entity, detail::PackedTransformSelfCheckComponent{glm::vec3{f, f * 2.0F, f * 3.0F}});
    }

    int checked = 0;
    for (auto [entity, transform] : registry.view<detail::PackedTransformSelfCheckComponent>().each()) {
        const float f = expected_base_values.at(entity);
        if (transform.position.x != f || transform.position.y != f * 2.0F ||
            transform.position.z != f * 3.0F) {
            return false; // a value didn't round-trip correctly through EnTT's storage
        }
        ++checked;
    }

    return checked == kEntityCount;
}

} // namespace konative::ecs
