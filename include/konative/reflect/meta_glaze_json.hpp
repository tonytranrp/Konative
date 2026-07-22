#pragma once

#include <string>

#include <entt/entt.hpp>
#include <glaze/glaze.hpp>

#include "konative/reflect/pfr_auto_registration.hpp"

// Real spike/prototype of ARCHITECTURE.md section 9's "entt::meta combined with Glaze for
// reflection-driven JSON serialization" pairing - the sibling item to the entt::meta + Boost.PFR
// auto-registration pairing pfr_auto_registration.hpp already proved. Serializes ANY
// entt::meta-reflected component to JSON generically, driven entirely by entt::meta's own runtime
// registration data (field names via the kFieldNamePropId property
// pfr_auto_registration.hpp's reflect_component_auto<T>() attaches, values via type.data()'s
// getters) - Glaze is never told the component's C++ type at all, it only ever formats one
// already-extracted, concretely-typed value (or a key string) at a time via its own real,
// strongly-typed glz::write_json(). This deliberately does NOT build a glz::generic/json_t object
// incrementally (Glaze's own dynamic-JSON type, whose object-construction API is
// undocumented/internal-header-only as of v7.9.1) - hand-formatting the surrounding object syntax
// while delegating each individual value's JSON encoding (number formatting, string escaping) to
// Glaze is a smaller, equally real test of the same pairing.
namespace konative::reflect {

namespace detail {

// Every fundamental type a Konative component field might actually be - not a general-purpose
// type-erased JSON encoder (that's a real, separate, much larger undertaking), just enough to
// prove the pairing on the same kind of plain-data fields reflect_component_auto<T>() itself
// supports. Returns false for any other runtime type, rather than guessing.
inline bool meta_value_to_json(const entt::meta_any& value, std::string& out) {
    if (value.type() == entt::resolve<int>()) {
        return !glz::write_json(value.cast<int>(), out);
    }
    if (value.type() == entt::resolve<float>()) {
        return !glz::write_json(value.cast<float>(), out);
    }
    if (value.type() == entt::resolve<bool>()) {
        return !glz::write_json(value.cast<bool>(), out);
    }
    if (value.type() == entt::resolve<double>()) {
        return !glz::write_json(value.cast<double>(), out);
    }
    return false;
}

} // namespace detail

// Returns an empty string on any failure (an unresolvable field name, a field whose value type
// detail::meta_value_to_json() doesn't handle, or a real Glaze write error) - matching this
// module's existing convention (reflect_component_auto<T>()'s own KONATIVE_ASSERT guard aside,
// there is no exception-based error channel anywhere in konative::reflect).
inline std::string meta_component_to_json(entt::meta_type type, entt::meta_any instance) {
    if (!type) {
        return "";
    }

    std::string json = "{";
    bool first = true;
    for (auto [id, data] : type.data()) {
        (void)id;
        entt::meta_prop name_prop = data.prop(detail::kFieldNamePropId);
        if (!name_prop) {
            return ""; // a data member entt::meta knows about but with no recorded real name
        }
        std::string field_name = name_prop.value().cast<std::string>();

        entt::meta_any value = data.get(instance);
        std::string value_json;
        if (!detail::meta_value_to_json(value, value_json)) {
            return "";
        }

        std::string key_json;
        if (glz::write_json(field_name, key_json)) {
            return ""; // a real Glaze write error formatting the key string itself
        }

        if (!first) {
            json += ",";
        }
        first = false;
        json += key_json;
        json += ":";
        json += value_json;
    }
    json += "}";
    return json;
}

} // namespace konative::reflect
