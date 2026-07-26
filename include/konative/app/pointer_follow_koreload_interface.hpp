#pragma once

#include "konative/ecs/registry.hpp"

// The interface KoReload's src/koreload_modules/pointer_follow/module.cpp hands back through
// koreload::PluginContract::instance, and src/platform/android/jni_onload.cpp casts that void*
// back to (KONATIVE_ENABLE_KORELOAD only - PROMPT.md section 13 M8 in the KoReload repo). Shared
// here, not defined independently on each side, so both the module and the host agree on the
// exact same struct layout from the exact same header - the same reasoning
// pointer_follow.hpp/pointer_follow_system.hpp are already split out for.
//
// Not an object in the usual OOP sense - a single function pointer matching
// move_followers_toward_targets' own (Registry&, float) system shape exactly. The host wraps this
// in ONE koreload::InterfaceHandle<PointerFollowKoreloadInterface>, repointed on every successful
// load/reload; world().systems().add() itself is called exactly once for the life of the process
// (SystemSequence has no removal API - see jni_onload.cpp's own setup comment for why calling
// add() again per reload would be wrong).
struct PointerFollowKoreloadInterface {
    void (*tick)(konative::ecs::Registry* registry, float delta_seconds);
};
