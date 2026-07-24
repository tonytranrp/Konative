#include <doctest/doctest.h>

#include <entt/entt.hpp>

#include "konative/reflect/meta_glaze_json_self_check.hpp"

TEST_CASE("run_meta_glaze_json_self_check: an entt::meta-reflected component serializes to real, "
          "round-trippable JSON via Glaze") {
    CHECK(konative::reflect::run_meta_glaze_json_self_check());
}

namespace {

// Real namespace scope, not a local type inside a TEST_CASE body - see
// meta_glaze_json_self_check.hpp's own detail::ParsedShape comment for the real, GCC-specific
// external-linkage reason every test-local reflected type in this codebase now follows this rule.
struct Nested {
    int value = 0;
};

// meta_value_to_json() (meta_glaze_json.hpp) only recognizes int/float/bool/double - a field of
// any other type, including a nested aggregate, is a real, documented limitation, not an
// oversight (see that file's own top comment: "not a general-purpose type-erased JSON encoder").
struct ComponentWithNestedField {
    int count = 0;
    Nested extra{};
};

// For the non-object-top-level rejection cases below - same external-linkage rule as Nested above.
struct PlainCounters {
    int first = 1;
    int second = 2;
};

} // namespace

// A real gap found by a self-audit (2026-07-22): reflect_component_auto<T>() is structurally
// generic (PFR/entt::meta both handle arbitrary field types), so a struct-typed field
// auto-registers with no error at all - giving every appearance of working. But
// meta_component_to_json() only understands int/float/bool/double per field, and falls through to
// an unconditional empty-string return for the WHOLE object the instant any one field doesn't fit
// (meta_glaze_json.hpp's meta_value_to_json() returning false propagates up through
// meta_component_to_json()'s own early return) - not a per-field skip. This test locks that
// combination in as a documented, intentional contract instead of an untested assumption.
TEST_CASE("meta_component_to_json returns empty for a component with an unsupported nested-struct field") {
    constexpr entt::id_type kId = entt::hashed_string{"test::ComponentWithNestedField"};
    konative::reflect::reflect_component_auto<ComponentWithNestedField>(kId); // registers cleanly, no error

    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));

    ComponentWithNestedField instance{};
    instance.count = 5;
    instance.extra.value = 10;

    CHECK(konative::reflect::meta_component_to_json(type, instance).empty());
}

// Found empirically on-device (2026-07-23), not hypothetically: a shell-quoting accident rewrote a
// real config file as `"tick_log_interval":60 ...` - a bare top-level JSON STRING with trailing
// garbage - and glz::read_json accepted it, so meta_component_from_json walked an object that
// wasn't there, matched zero fields, and reported SUCCESS for content that was garbage. A
// component is a JSON object; any other top-level value must be a parse failure, not a silent
// zero-field "partial update".
TEST_CASE("meta_component_from_json rejects valid JSON whose top level is not an object") {
    constexpr entt::id_type kId = entt::hashed_string{"test::PlainCounters"};
    konative::reflect::reflect_component_auto<PlainCounters>(kId);
    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));

    PlainCounters instance{};
    CHECK_FALSE(konative::reflect::meta_component_from_json(type, instance, R"("just a string")"));
    CHECK_FALSE(konative::reflect::meta_component_from_json(type, instance, "42"));
    CHECK_FALSE(konative::reflect::meta_component_from_json(type, instance, "[1,2,3]"));
    // The exact empirical shape that motivated this: bare string + trailing garbage.
    CHECK_FALSE(konative::reflect::meta_component_from_json(
        type, instance, R"("tick_log_interval":60 "snapshot_interval_ticks":600)"));
    // Untouched throughout.
    CHECK(instance.first == 1);
    CHECK(instance.second == 2);

    // An EMPTY object stays legitimate: zero fields present is the partial-update contract's
    // honest degenerate case (nothing to set), not an error.
    CHECK(konative::reflect::meta_component_from_json(type, instance, "{}"));
    CHECK(instance.first == 1);
    CHECK(instance.second == 2);
}
