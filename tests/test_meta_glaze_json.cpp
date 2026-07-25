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

// For the out-of-range-number rejection cases below - a real int and a real float field, the two
// json_value_to_meta() branches a deep-review audit pass (2026-07-25) found perform an unchecked,
// genuinely-UB narrowing cast from glz::generic's internal double storage for any out-of-range
// JSON number.
struct NumericFields {
    int whole = 1;
    float fraction = 1.0F;
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

// A deep-review audit pass (2026-07-25) found json_value_to_meta()'s int/float branches performed
// an unchecked static_cast<T>(double) - genuinely UB per the standard for a source value outside
// the target type's representable range, not just "produces a weird number". Confirmed directly
// against Glaze's own vendored json/generic_fwd.hpp: as<int>()/as<float>() resolve, via the same
// more-constrained-wins overload rule already documented above for bool, to an unconditional
// `static_cast<T>(get<double>())` for this project's real f64 mode. A JSON number a real user (or
// a corrupted/malicious config file) could genuinely write - e.g. 1e30 - is exactly this shape.
TEST_CASE("meta_component_from_json rejects an out-of-range JSON number instead of an unchecked narrowing cast") {
    constexpr entt::id_type kId = entt::hashed_string{"test::NumericFields"};
    konative::reflect::reflect_component_auto<NumericFields>(kId);
    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));

    // Comfortably outside int's representable range.
    {
        NumericFields instance{};
        CHECK_FALSE(konative::reflect::meta_component_from_json(type, instance, R"({"whole":1e30})"));
        CHECK(instance.whole == 1); // untouched - meta_component_from_json's own failure contract
    }
    {
        NumericFields instance{};
        CHECK_FALSE(konative::reflect::meta_component_from_json(type, instance, R"({"whole":-1e30})"));
        CHECK(instance.whole == 1);
    }
    // Comfortably outside float's representable range (but well within double's).
    {
        NumericFields instance{};
        CHECK_FALSE(
            konative::reflect::meta_component_from_json(type, instance, R"({"fraction":1e300})"));
        CHECK(instance.fraction == 1.0F);
    }
    // In-range values on both sides of zero still work correctly - this is a range check, not a
    // blanket rejection of anything large-ish.
    {
        NumericFields instance{};
        REQUIRE(konative::reflect::meta_component_from_json(
            type, instance, R"({"whole":2000000000,"fraction":-123.5})"));
        CHECK(instance.whole == 2000000000);
        CHECK(instance.fraction == doctest::Approx(-123.5F));
    }
}
