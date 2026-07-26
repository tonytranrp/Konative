#pragma once

#include "konative/app/pointer_follow.hpp"
#include "konative/ecs/registry.hpp"
#include "konative/spatial/approach.hpp"
#include "konative/spatial/transform.hpp"

// The real per-entity glide logic, shared verbatim by both the default (statically-linked) system
// registration in src/platform/android/jni_onload.cpp and the KoReload-managed module in
// src/koreload_modules/pointer_follow/ (KONATIVE_ENABLE_KORELOAD) - a compile-time-only #include on
// both sides, never a runtime shared dependency between the host .so and the module .so (KoReload's
// own "core must be compile-time-only" requirement - PROMPT.md section 17.1 in the KoReload repo).
// Header-only so there is exactly one real implementation to drift, not two hand-kept-in-sync
// copies - same reasoning as spatial::approach()/to_matrix() already being free functions here.
namespace konative::app {

inline void move_followers_toward_targets(konative::ecs::Registry& registry, float delta_seconds) {
    for (auto [entity, transform, follow] :
         registry.view<konative::spatial::Transform, PointerFollow>().each()) {
        konative::spatial::approach(transform, follow.target, follow.approach_rate, delta_seconds);
    }
}

} // namespace konative::app
