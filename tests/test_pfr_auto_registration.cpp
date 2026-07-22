#include <doctest/doctest.h>

#include <boost/pfr.hpp>
#include <entt/entt.hpp>

#include "konative/reflect/pfr_auto_registration_self_check.hpp"

TEST_CASE("run_pfr_auto_registration_self_check: PFR-derived fields round-trip through entt::meta") {
    CHECK(konative::reflect::run_pfr_auto_registration_self_check());
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
