#pragma once

#include <algorithm>

// Real runtime configuration for a Konative Application instance - the first real, non-synthetic
// consumer of the entt::meta + Boost.PFR auto-registration and entt::meta + Glaze JSON pairings
// (reflect/pfr_auto_registration.hpp, reflect/meta_glaze_json.hpp), which until now had only ever
// been exercised against their own self-check-only test types (PfrSelfCheckComponent,
// GlazeSelfCheckComponent in reflect/*_self_check.hpp) - never a real component an actual app
// cares about. Also the first real call site for entt::registry::ctx() (ecs/world.hpp's own doc
// comment names it as Konative's intended DI/composition-root mechanism - "register cross-cutting
// services via registry().ctx().emplace<Service>(...)" - with zero real usage anywhere before this).
//
// Field types are deliberately limited to int (not e.g. std::uint64_t) - meta_glaze_json.hpp's
// detail::meta_value_to_json()/json_value_to_meta() only handle int/float/bool/double today (see
// that file's own comment on why - not a general-purpose type-erased JSON encoder), and both real
// values here (a tick count, a frame count) fit comfortably in an int with room to spare.
namespace konative::app {

// Defaults match src/platform/android/jni_onload.cpp's previously-hardcoded
// kTickLogInterval/kSnapshotIntervalTicks constants exactly, so landing this changes nothing about
// default behavior - only makes both values genuinely overridable via real JSON instead of requiring
// a recompile.
struct AppConfig {
    int tick_log_interval = 120;       // KonativeAndroidApp::on_tick()'s periodic summary cadence
    int snapshot_interval_ticks = 300; // KonativeAndroidApp::on_tick()'s periodic snapshot cadence
};

// A floor of 1 alone only prevents the division-by-zero UB - it does NOT prevent a real resource-
// exhaustion path config/json_config_file.hpp's hot-reload made genuinely reachable by a simple
// file edit (previously this value was compiled-in, always the sane default, never user input):
// jni_onload.cpp's on_tick() spawns a real detached std::thread doing blocking file I/O (an atomic
// temp-file-then-rename write) every `snapshot_interval_ticks` ticks. At this project's own
// documented real refresh rates (Choreographer, ~60-120Hz on real hardware - see on_tick()'s own
// comment), an unclamped value of 1 means 60-120 new OS threads per second, each doing disk I/O,
// spawned indefinitely for as long as the app runs - real thread/fd churn a config edit should
// never be able to trigger. 30 caps this at a bounded 2-4 writer-threads/sec even at this
// project's fastest observed refresh rate, while still leaving the interval genuinely live-tunable
// down to something much more frequent than the 300 compiled-in default.
inline constexpr int kMinSnapshotIntervalTicks = 30;

// tick_log_interval's floor doesn't guard a resource leak (logging spawns no thread) - just
// logcat-flood risk, the same "downgrade high-frequency noise" lesson this codebase already
// learned once for FrameTicker's own JNI-binding-failure logging. 10 bounds it to at most ~12
// lines/sec at 120Hz, well short of a real flood, while staying far more permissive than
// snapshotting's own floor since logging is comparatively cheap.
inline constexpr int kMinTickLogInterval = 10;

// Both fields are `tick_count % interval` divisors in KonativeAndroidApp::on_tick() - see each
// constant's own comment above for why its specific floor is what it is, not just "> 0". A free
// function, not a member - components stay plain data with behavior expressed separately, the same
// convention as spatial::to_matrix() (spatial/README.md's own Hard Rule shape).
inline void clamp_to_valid(AppConfig& config) {
    config.tick_log_interval = std::max(config.tick_log_interval, kMinTickLogInterval);
    config.snapshot_interval_ticks = std::max(config.snapshot_interval_ticks, kMinSnapshotIntervalTicks);
}

} // namespace konative::app
