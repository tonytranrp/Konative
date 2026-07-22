#include <doctest/doctest.h>
#include <entt/entt.hpp>

#include "konative/reflect/meta_registry.hpp"

using namespace entt::literals;

namespace {

// A minimal ReflectableComponent (aggregate, default-constructible) - see
// include/konative/reflect/component_traits.hpp.
struct Health {
    int value = 100;
};

constexpr entt::id_type kHealthId = entt::hashed_string{"test::Health"};

} // namespace

// entt::meta's registration context is process-global (not per-TEST_CASE), so both TEST_CASEs
// below genuinely share it across the same test binary - this is deliberate, not an oversight (see
// the second TEST_CASE, which specifically exercises that sharing).
TEST_CASE("reflect_component registers a resolvable type and a working emplace thunk") {
    konative::reflect::reflect_component<Health>(kHealthId);

    auto type = entt::resolve(kHealthId);
    REQUIRE(static_cast<bool>(type));

    entt::registry registry;
    entt::entity entity = registry.create();
    CHECK_FALSE(registry.all_of<Health>(entity));

    auto emplace_func = type.func("emplace"_hs);
    REQUIRE(static_cast<bool>(emplace_func));

    // "emplace" was registered as a free function (detail::emplace_by_type_thunk), not a member
    // function - invoke with an empty handle as the instance, per EnTT's own convention for this.
    emplace_func.invoke({}, entt::forward_as_meta(registry), entity);

    CHECK(registry.all_of<Health>(entity));
    CHECK(registry.get<Health>(entity).value == 100);
}

// A real code-review pass (2026-07-22) found this codebase's OWN doc comments (this file included,
// previously) mischaracterized what actually needs guarding against: NOT "calling
// reflect_component<T>() twice for the same type," which a direct read of EnTT's real
// meta_factory<T>::type() source (and this empirical test, calling it twice in a row for the same
// type/id right here) confirms is a safe no-op - entt::meta<T>() pre-registers T in the context
// before the id even gets (re-)assigned, so re-assigning it to the SAME id it already owns
// satisfies that assertion's own condition. reflect_component<T>()'s own real guard now (a
// KONATIVE_ASSERT, release-mode-safe unlike EnTT's own internal one) is for the genuinely different
// case: two DIFFERENT types colliding on the same id - not exercised here, since KONATIVE_ASSERT's
// abort()-on-failure design makes that failure path unsafe to trigger inside a normal test process.
TEST_CASE("reflect_component is safely idempotent for the SAME type re-registered under the SAME id") {
    konative::reflect::reflect_component<Health>(kHealthId);
    konative::reflect::reflect_component<Health>(kHealthId); // the real, empirically-verified case

    auto type = entt::resolve(kHealthId);
    REQUIRE(static_cast<bool>(type));
    CHECK(type.info() == entt::type_id<Health>());

    entt::registry registry;
    entt::entity entity = registry.create();
    auto emplace_func = type.func("emplace"_hs);
    REQUIRE(static_cast<bool>(emplace_func));
    emplace_func.invoke({}, entt::forward_as_meta(registry), entity);
    CHECK(registry.all_of<Health>(entity));
}
