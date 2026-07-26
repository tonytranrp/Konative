#pragma once

#include <glm/glm.hpp>

// PointerFollow pairs with a konative::spatial::Transform on a demo entity whose position glides
// toward `target` every tick via konative::spatial::approach() - see
// src/platform/android/jni_onload.cpp's own move_followers_toward_targets() (the default, always-
// linked system) and, when KONATIVE_ENABLE_KORELOAD is ON, src/koreload_modules/pointer_follow/
// (the same logic, hot-reloadable via KoReload instead of statically linked - PROMPT.md section 13
// M8 in the KoReload repo). Moved out of jni_onload.cpp's own anonymous namespace into a real,
// shared header specifically so both sides of that dlopen boundary compile the identical struct
// layout from the identical header - a hand-copied duplicate risks silent layout drift the moment
// either side's copy is edited without the other.
namespace konative::app {

struct PointerFollow {
    glm::vec3 target{0.0F, 0.0F, 0.0F};
    // ~8 time constants per second: visibly smooth glide that settles in roughly a quarter
    // second - demo-tuned, and genuinely overridable at runtime once a config need appears.
    float approach_rate = 8.0F;
};

} // namespace konative::app
