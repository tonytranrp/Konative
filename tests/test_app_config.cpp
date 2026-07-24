#include <doctest/doctest.h>

#include <entt/entt.hpp>

#include "konative/app/app_config.hpp"
#include "konative/ecs/registry.hpp"
#include "konative/reflect/meta_glaze_json.hpp"
#include "konative/reflect/pfr_auto_registration.hpp"

// AppConfig (include/konative/app/app_config.hpp) is the first REAL, non-synthetic component the
// entt::meta + Boost.PFR auto-registration and entt::meta + Glaze JSON pairings are exercised
// against - every desktop test for those two pairings before this file used a test-local,
// self-check-only type (PfrSelfCheckComponent, GlazeSelfCheckComponent/ComponentWithNestedField).
// These cases prove the exact real usage src/platform/android/jni_onload.cpp's on_started() relies
// on, on desktop (GCC/Clang via CI), not just verified once on-device.

TEST_CASE("AppConfig round-trips through entt::meta + Boost.PFR + Glaze JSON with real default values") {
    constexpr entt::id_type kId = entt::hashed_string{"test::AppConfig::defaults"};
    konative::reflect::reflect_component_auto<konative::app::AppConfig>(kId);
    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));

    konative::app::AppConfig config{};
    CHECK(config.tick_log_interval == 120);
    CHECK(config.snapshot_interval_ticks == 300);

    // Round-trips the real struct defaults through the same write path
    // meta_component_to_json() offers, even though jni_onload.cpp itself only ever reads (the
    // config source is a compiled-in JSON literal, not a value it serializes back out) - proves the
    // write direction stays correct against this real component too, not just the self-checks'
    // synthetic ones.
    std::string json = konative::reflect::meta_component_to_json(type, config);
    CHECK_FALSE(json.empty());

    konative::app::AppConfig round_tripped{};
    round_tripped.tick_log_interval = -1;    // deliberately wrong, must be overwritten below
    round_tripped.snapshot_interval_ticks = -1;
    REQUIRE(konative::reflect::meta_component_from_json(type, round_tripped, json));
    CHECK(round_tripped.tick_log_interval == 120);
    CHECK(round_tripped.snapshot_interval_ticks == 300);
}

TEST_CASE("AppConfig: a partial JSON override changes only the field present, matching jni_onload.cpp's real usage") {
    constexpr entt::id_type kId = entt::hashed_string{"test::AppConfig::partial"};
    konative::reflect::reflect_component_auto<konative::app::AppConfig>(kId);
    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));

    // The exact shape of JSON jni_onload.cpp's on_started() actually parses (kDefaultAppConfigJson)
    // - only tick_log_interval present, snapshot_interval_ticks deliberately absent.
    konative::app::AppConfig config{};
    REQUIRE(konative::reflect::meta_component_from_json(type, config, R"({"tick_log_interval":180})"));

    CHECK(config.tick_log_interval == 180);          // overridden by the JSON
    CHECK(config.snapshot_interval_ticks == 300);    // left at its struct default - not an error
}

TEST_CASE("AppConfig: clamp_to_valid() forces both intervals to >= 1 - the on_tick() division-by-zero guard") {
    // Both fields are `tick_count % interval` divisors in jni_onload.cpp's on_tick(), and both are
    // now genuinely user-editable (config/json_config_file.hpp's real file) - a zero or negative
    // value is a real input a config edit can produce, and unclamped it would be integer division
    // by zero (real UB), not just a weird cadence.
    konative::app::AppConfig config{};
    config.tick_log_interval = 0;
    config.snapshot_interval_ticks = -50;
    konative::app::clamp_to_valid(config);
    CHECK(config.tick_log_interval == 1);
    CHECK(config.snapshot_interval_ticks == 1);

    // Already-valid values pass through untouched.
    config.tick_log_interval = 120;
    config.snapshot_interval_ticks = 300;
    konative::app::clamp_to_valid(config);
    CHECK(config.tick_log_interval == 120);
    CHECK(config.snapshot_interval_ticks == 300);
}

TEST_CASE("AppConfig: entt::registry::ctx() stores and retrieves it - the real DI mechanism ecs/world.hpp documents") {
    // world.hpp's own doc comment names registry().ctx() as Konative's intended cross-cutting-
    // service/DI mechanism ("register cross-cutting services via
    // registry().ctx().emplace<Service>(...)") - AppConfig is the first real component to actually
    // exercise it anywhere in this codebase (confirmed by repo-wide grep before landing this).
    konative::ecs::Registry registry;

    konative::app::AppConfig config{};
    config.tick_log_interval = 42;
    registry.ctx().emplace<konative::app::AppConfig>(config);

    const auto& fetched = registry.ctx().get<konative::app::AppConfig>();
    CHECK(fetched.tick_log_interval == 42);
    CHECK(fetched.snapshot_interval_ticks == 300);
}
