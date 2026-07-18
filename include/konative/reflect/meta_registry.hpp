#pragma once

#include <entt/entt.hpp>

#include "konative/reflect/component_traits.hpp"
#include "konative/reflect/detail/registration_thunk.hpp"

// The registration surface used to reflect a component type AND register its
// emplace-into-registry thunk in one call - see ARCHITECTURE.md section 3 for why both halves are
// needed (entt::meta reflection alone is not enough for generic registry mutation).
namespace konative::reflect {

template <ReflectableComponent T>
void reflect_component(entt::id_type id) {
    using namespace entt::literals; // for "emplace"_hs below - NOT automatically in scope just
    // because entt/entt.hpp is included (a real compile error a code review's new
    // test_reflect.cpp caught, since this template had never been instantiated by anything
    // before that test existed).
    //
    // entt::meta<T>() (the free function), NOT entt::meta_factory<T>{} (direct construction) -
    // this is the actual root cause of a real bug this function's first-ever compile+run (via
    // test_reflect.cpp) caught: entt::meta<T>()'s implementation does
    // `context.value.try_emplace(type_id<Type>().hash(), ...)` to pre-register T in the meta
    // context BEFORE constructing the factory ("make sure the type exists in the context before
    // returning a factory" per EnTT's own factory.hpp comment); entt::meta_factory<T>{}'s raw
    // constructor skips that step and instead ASSERTS the entry already exists
    // (internal::owner()'s "Type not available" assertion) - so it only works for a type that's
    // already been registered some other way. entt::meta<T>() is the correct public entry point
    // for first-time registration.
    // `template` disambiguator before func<...>: meta_factory<T> is dependent on T, and func<...>
    // is a template member called with an explicit argument list.
    entt::meta<T>().type(id).template func<&detail::emplace_by_type_thunk<T>>("emplace"_hs);
}

} // namespace konative::reflect
