#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include <boost/pfr.hpp>
#include <entt/entt.hpp>

#include "konative/core/assert.hpp"
#include "konative/reflect/component_traits.hpp"
#include "konative/reflect/detail/registration_thunk.hpp"

// Real spike/prototype of ARCHITECTURE.md section 9's explicitly-flagged "genuinely unproven"
// entt::meta + Boost.PFR pairing ("auto-registration... neither philosophically clean pairing
// found done anywhere" - the doc's own words asking for exactly this before committing further
// architecture on top of the assumption that it works).
//
// reflect_component<T>() (meta_registry.hpp) already handles the TYPE-level registration (the
// "emplace by id" thunk) - this file adds FIELD-level auto-registration on top of the same
// factory, so a generic, reflection-driven consumer (editor tooling, deserialization) can also
// enumerate/get/set a component's real fields by name without the component's author ever having
// hand-written a single entt::meta<T>().data<&T::field>(id) call.
//
// Boost.PFR's own reflection (boost::pfr::get<I>(instance), boost::pfr::get_name<I, T>()) is
// STRUCTURAL, not name-based at the type-system level - there is no way to turn a PFR-derived
// field name back into a real pointer-to-data-member (&T::field) the way entt::meta's
// .data<&Data>(id) overload wants, since C++20 has no "synthesize an identifier from a string"
// reflection primitive. The fix (verified against EnTT's own vendored meta/factory.hpp,
// 2026-07-22): a DIFFERENT, already-real entt::meta_factory<T>::data() overload -
// .data<Setter, Getter>(id) - accepts a getter/setter FUNCTION POINTER PAIR instead of a
// pointer-to-member. konative::reflect::detail::pfr_get_field<T, I>/pfr_set_field<T, I> below are
// real function templates (not lambdas - a real, plain function pointer is what the Setter/Getter
// non-type template parameters need) that close over the field INDEX and dispatch through
// boost::pfr::get<I>() structurally, giving entt::meta a real getter/setter pair per field without
// ever naming the field as a C++ identifier.
namespace konative::reflect::detail {

template <typename T, std::size_t I>
using pfr_field_t = std::remove_cvref_t<decltype(boost::pfr::get<I>(std::declval<T&>()))>;

template <typename T, std::size_t I>
pfr_field_t<T, I> pfr_get_field(T& instance) {
    return boost::pfr::get<I>(instance);
}

template <typename T, std::size_t I>
void pfr_set_field(T& instance, pfr_field_t<T, I> value) {
    boost::pfr::get<I>(instance) = value;
}

// entt::hashed_string's (const char*, size_type) constructor hashes exactly `len` bytes starting
// at `str` - it does NOT require a NUL terminator (confirmed against the real vendored
// core/hashed_string.hpp, 2026-07-22), which matters here since boost::pfr::get_name<I, T>()
// returns a std::string_view, not a NUL-terminated C string.
template <typename T, std::size_t I>
entt::id_type pfr_field_id() {
    constexpr auto name = boost::pfr::get_name<I, T>();
    return entt::hashed_string{name.data(), name.size()};
}

template <typename T, std::size_t... Is>
void reflect_fields_auto_impl(entt::meta_factory<T> factory, std::index_sequence<Is...>) {
    // Comma-fold: each .data<>() call's return value is discarded (every call already returns
    // *this, the same factory - see the .type()/.func() chain below), the fold just forces all
    // of them to run, in field-declaration order, as a side effect against entt::meta's real,
    // process-global registration context.
    (factory.template data<&pfr_set_field<T, Is>, &pfr_get_field<T, Is>>(pfr_field_id<T, Is>()), ...);
}

} // namespace konative::reflect::detail

namespace konative::reflect {

// Same type-level registration + collision guard as reflect_component<T>() (meta_registry.hpp) -
// deliberately not implemented in terms of that function, since this one always needs the
// entt::meta_factory<T> chain kept alive locally to append field registrations onto, and
// duplicating the ~10 lines of guard logic here is clearer than threading a factory reference
// through reflect_component<T>()'s own signature just for this one caller.
template <ReflectableComponent T>
void reflect_component_auto(entt::id_type id) {
    using namespace entt::literals;
    if (const entt::meta_type existing = entt::resolve(id); existing) {
        KONATIVE_ASSERT(existing.info() == entt::type_id<T>(),
                         "reflect_component_auto<T>(): id already belongs to a DIFFERENT type - two "
                         "components collided on the same custom entt::id_type");
    }

    auto factory = entt::meta<T>().type(id).template func<&detail::emplace_by_type_thunk<T>>("emplace"_hs);
    detail::reflect_fields_auto_impl<T>(factory, std::make_index_sequence<boost::pfr::tuple_size_v<T>>{});
}

} // namespace konative::reflect
