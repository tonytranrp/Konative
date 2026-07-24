#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "konative/spatial/transform.hpp"

namespace konative::spatial {

// Moves `transform.position` toward `target` by an exponential-smoothing step: the standard
// frame-rate-independent approach curve (position converges toward target with time constant
// 1/rate, never overshooting), chosen over a naive `position += direction * speed * dt` because
// the naive form both overshoots at large dt and changes its settle behavior with frame rate.
// Frame-rate independence here is exact, not approximate: applying dt1 then dt2 lands at exactly
// the same point as applying (dt1 + dt2) in one step, because the retained fraction composes
// multiplicatively (e^(-r*dt1) * e^(-r*dt2) == e^(-r*(dt1+dt2))) - locked in by a real desktop
// test, not just asserted.
//
// A free function operating on Transform data, not a member and not a System: same convention as
// to_matrix() (spatial/README.md's own Hard Rule - this folder is spatial data and pure operations
// on it; the SYSTEM that iterates a registry calling this per-entity belongs to whatever module
// owns that concern, e.g. the app itself for jni_onload.cpp's pointer-follower demo entity).
//
// rate <= 0 means "hold still" (retained fraction 1) rather than being clamped or asserted - a
// zero rate is a legitimate way to freeze an entity without removing its component.
inline void approach(Transform& transform, const glm::vec3& target, float rate,
                     float delta_seconds) {
    if (rate <= 0.0F || delta_seconds <= 0.0F) {
        return;
    }
    const float factor = 1.0F - std::exp(-rate * delta_seconds);
    transform.position = glm::mix(transform.position, target, factor);
}

} // namespace konative::spatial
