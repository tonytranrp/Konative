#pragma once

#include <cstddef>

#include <boost/pfr.hpp>
#include <entt/entt.hpp>

#include "konative/reflect/pfr_auto_registration.hpp"

// A real, permanent self-check that Boost.PFR + entt::meta compose correctly for automatic field
// registration - ARCHITECTURE.md section 9's explicitly-flagged "genuinely unproven" pairing (a
// real spike/prototype needed before committing further architecture on top of the assumption it
// works). Same "code checks itself" precedent as
// scheduling/taskflow_self_check.hpp/ecs/registry_snapshot_self_check.hpp.
namespace konative::reflect {

namespace detail {

// A test-local component with 3 fields of 3 different types, deliberately more than one - this
// self-check only needs to prove the auto-registration MECHANISM correctly enumerates every field
// with the right name/type/value, not exercise a real gameplay component.
struct PfrSelfCheckComponent {
    int a = 0;
    float b = 0.0F;
    bool c = false;
};

} // namespace detail

// Verifies, in order: (1) PFR's own field count matches how many entt::meta data members actually
// landed after auto-registration - the mechanism didn't silently drop or duplicate a field; (2)
// every field is resolvable by its REAL, PFR-derived name (not a synthetic index-based one); (3)
// setting each field through the getter/setter pair auto-registration installed genuinely mutates
// the real underlying object, confirmed via ordinary, direct C++ member access (not just "the
// setter returned true"); (4) the meta getter reads back the same value, confirming both
// directions round-trip correctly.
//
// meta_data::set()/get() take a real instance directly (relying on meta_handle's own implicit
// Type& constructor), NOT entt::forward_as_meta(instance) - a real, empirically-found difference
// (2026-07-22): forward_as_meta() produces a meta_any PRVALUE, and meta_handle's
// const meta_any& constructor built from THAT temporary does not correctly enable mutation through
// the resulting handle (set() silently returns false, confirmed via a from-scratch minimal repro
// using entt::meta's own direct pointer-to-data-member registration - not specific to PFR or to
// the Setter/Getter free-function-pair overload at all). forward_as_meta() itself works fine for
// passing an instance to a meta FUNCTION call (meta_registry.hpp's own "emplace" thunk invocation,
// test_reflect.cpp), just not as meta_data::set()/get()'s instance argument.
inline bool run_pfr_auto_registration_self_check() {
    using namespace entt::literals;
    constexpr entt::id_type kId = entt::hashed_string{"konative::reflect::PfrSelfCheckComponent"};
    reflect_component_auto<detail::PfrSelfCheckComponent>(kId);

    auto type = entt::resolve(kId);
    if (!type) {
        return false;
    }

    std::size_t registered_count = 0;
    for (auto data : type.data()) {
        (void)data;
        ++registered_count;
    }
    if (registered_count != boost::pfr::tuple_size_v<detail::PfrSelfCheckComponent>) {
        return false; // PFR says 3 fields; auto-registration didn't land exactly 3 entt::meta data members
    }

    entt::meta_data field_a = type.data("a"_hs);
    entt::meta_data field_b = type.data("b"_hs);
    entt::meta_data field_c = type.data("c"_hs);
    if (!field_a || !field_b || !field_c) {
        return false; // a field isn't resolvable under its real, PFR-derived name
    }

    detail::PfrSelfCheckComponent instance{};
    if (!field_a.set(instance, 42) || !field_b.set(instance, 3.5F) || !field_c.set(instance, true)) {
        return false;
    }

    // Direct C++ access, not another meta call - confirms the setter genuinely reached the real
    // object, not a detached temporary the setter thunk silently discarded.
    if (instance.a != 42 || instance.b != 3.5F || !instance.c) {
        return false;
    }

    entt::meta_any got_a = field_a.get(instance);
    entt::meta_any got_b = field_b.get(instance);
    entt::meta_any got_c = field_c.get(instance);
    return got_a.cast<int>() == 42 && got_b.cast<float>() == 3.5F && got_c.cast<bool>() == true;
}

} // namespace konative::reflect
