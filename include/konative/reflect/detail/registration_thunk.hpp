#pragma once

#include <entt/entt.hpp>

// entt::registry has no way to construct a component pool from only a reflected type_info/hash -
// it needs the concrete C++ type at the call site (confirmed directly against EnTT's own
// maintainer, see ARCHITECTURE.md \xc2\xa73). This thunk closes over the concrete type T so generic,
// reflection-driven code (editor "add component" UI, deserialization) can emplace-by-id without
// the caller ever naming T itself.
namespace konative::reflect::detail {

template <typename T>
void emplace_by_type_thunk(entt::registry& registry, entt::entity entity) {
    registry.emplace<T>(entity);
}

} // namespace konative::reflect::detail
