#pragma once

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

} // namespace konative::app
