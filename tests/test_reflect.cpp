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

// entt::meta's registration context is process-global (not per-TEST_CASE) - calling
// reflect_component<Health>(kHealthId) more than once across separate TEST_CASEs trips an
// internal EnTT assertion (a real bug an earlier version of this file, with two separate
// TEST_CASEs each calling reflect_component, hit). Register exactly once, check everything in one
// TEST_CASE.
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
