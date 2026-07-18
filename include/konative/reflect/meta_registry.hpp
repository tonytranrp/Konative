#pragma once

#include <entt/entt.hpp>

#include "konative/reflect/component_traits.hpp"
#include "konative/reflect/detail/registration_thunk.hpp"

// The registration surface used to reflect a component type AND register its
// emplace-into-registry thunk in one call - see ARCHITECTURE.md \xc2\xa73 for why both halves are
// needed (entt::meta reflection alone is not enough for generic registry mutation).
namespace konative::reflect {

template <ReflectableComponent T>
void reflect_component(entt::id_type id) {
    entt::meta_factory<T>{}.type(id);
    // Register the emplace thunk as a plain entt::meta func so generic code can resolve
    // "construct T into this registry" purely from the reflected id - see detail/registration_thunk.hpp.
    entt::meta_factory<T>{}.func<&detail::emplace_by_type_thunk<T>>("emplace"_hs);
}

} // namespace konative::reflect
