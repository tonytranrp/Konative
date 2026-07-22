#include <doctest/doctest.h>

#include <boost/pfr.hpp>
#include <entt/entt.hpp>

#include "konative/reflect/pfr_auto_registration_self_check.hpp"

namespace {

// Real namespace scope, not a local type inside a TEST_CASE body - matching test_reflect.cpp's
// own established pattern (its file-scope anonymous-namespace `Health` struct) and the real,
// GCC-specific external-linkage lesson meta_glaze_json_self_check.hpp's own fix found (2026-07-22):
// Glaze needed it, and there's no reason to assume PFR/entt::meta never will for some future
// compiler/version, so every test-local reflected type in this codebase follows the same rule now.
struct EmptyComponent {};

} // namespace

TEST_CASE("run_pfr_auto_registration_self_check: PFR-derived fields round-trip through entt::meta") {
    CHECK(konative::reflect::run_pfr_auto_registration_self_check());
}

// A real gap found by a self-audit (2026-07-22): boost::pfr::tuple_size_v<T> for a genuinely
// empty aggregate is a real, PFR-handled case (confirmed against the vendored source,
// boost/pfr/detail/fields_count.hpp's own std::is_empty special-case), and
// reflect_fields_auto_impl<T>()'s comma-fold over std::index_sequence<Is...> correctly
// identity-reduces to void() for an empty parameter pack per the C++17 fold-expression rules -
// but nothing had actually exercised this before, only the 3-field PfrSelfCheckComponent shape.
TEST_CASE("reflect_component_auto handles a genuinely empty component correctly") {
    constexpr entt::id_type kId = entt::hashed_string{"test::EmptyComponent"};
    konative::reflect::reflect_component_auto<EmptyComponent>(kId);

    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));
    CHECK(type.info() == entt::type_id<EmptyComponent>());

    std::size_t count = 0;
    for (auto data : type.data()) {
        (void)data;
        ++count;
    }
    CHECK(count == 0);
    CHECK(boost::pfr::tuple_size_v<EmptyComponent> == 0);
}

// Mirrors test_reflect.cpp's own idempotency test for reflect_component<T>() - the SAME real
// EnTT behavior (entt::meta<T>() pre-registers T in the context before (re-)assigning an id, so
// re-assigning it to the id it already owns is a safe no-op) applies here too, since
// reflect_component_auto<T>() shares the exact same type-level registration + guard logic.
TEST_CASE("reflect_component_auto is safely idempotent for the SAME type re-registered under the SAME id") {
    using konative::reflect::detail::PfrSelfCheckComponent;
    constexpr entt::id_type kId = entt::hashed_string{"test::PfrAutoIdempotent"};

    konative::reflect::reflect_component_auto<PfrSelfCheckComponent>(kId);
    konative::reflect::reflect_component_auto<PfrSelfCheckComponent>(kId); // real, empirically-verified safe case

    auto type = entt::resolve(kId);
    REQUIRE(static_cast<bool>(type));
    CHECK(type.info() == entt::type_id<PfrSelfCheckComponent>());

    // Re-registering must not have duplicated the 3 auto-registered data members either.
    std::size_t count = 0;
    for (auto data : type.data()) {
        (void)data;
        ++count;
    }
    CHECK(count == boost::pfr::tuple_size_v<PfrSelfCheckComponent>);
}
