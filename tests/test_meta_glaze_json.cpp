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
