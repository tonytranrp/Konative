#pragma once

#include <string>

#include <entt/entt.hpp>
#include <glaze/glaze.hpp>

#include "konative/reflect/pfr_auto_registration.hpp"

// Real spike/prototype of ARCHITECTURE.md section 9's "entt::meta combined with Glaze for
// reflection-driven JSON serialization" pairing - the sibling item to the entt::meta + Boost.PFR
// auto-registration pairing pfr_auto_registration.hpp already proved. Serializes (meta_component_
// to_json) AND deserializes (meta_component_from_json) ANY entt::meta-reflected component to/from
// JSON generically, driven entirely by entt::meta's own runtime registration data (field names via
// the kFieldNamePropId property pfr_auto_registration.hpp's reflect_component_auto<T>() attaches,
// values via type.data()'s getters/setters) - the direction this pairing was actually chosen for in
// the first place (KonativeDependencies.cmake's own comment: "macro-free reflection-driven JSON,
// for config/hot-reload" - a config LOADER needs the read direction, not just a debug/inspection
// write path). Glaze is never told the component's C++ type at all on the write side - it only
// ever formats one already-extracted, concretely-typed value (or a key string) at a time via its
// own real, strongly-typed glz::write_json(); the read side uses glz::generic (Glaze's own
// dynamic-JSON type) to PARSE, but deliberately never builds one incrementally to WRITE (that
// object-construction API is undocumented/internal-header-only as of v7.9.1) - hand-formatting the
// surrounding object syntax on write, while delegating each individual value's JSON encoding
// (number formatting, string escaping) to Glaze either direction, is a smaller, equally real test
// of the same pairing.
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

// The read-side counterpart of meta_value_to_json() above - same fundamental-type coverage,
// same "return false rather than guess" policy for anything else.
//
// bool uses get<bool>(), NOT as<bool>() - a real runtime std::bad_variant_access found empirically
// (2026-07-22), not the compile-time-only SFINAE story the int/float cases below rely on.
// glz::generic's default numeric mode stores every JSON number as a real double, but a JSON true/
// false parses into the variant's actual bool alternative - so a bool VALUE is never reached via
// int/float's own "any T a stored double can static_cast to" as<T>() overload the way this
// comment used to assume. Worse, bool is ALSO one of the variant's real alternatives (verified
// directly against the vendored json/generic_fwd.hpp: `std::variant<null_t, double, std::string,
// bool, ...>`), so as<bool>() is genuinely AMBIGUOUS between two real overloads - and C++20's
// more-constrained-wins partial-ordering rule picks the double-conversion one, whose body calls
// get<double>() on a variant that's actually holding bool, throwing at runtime. get<bool>() is the
// only correct, unambiguous call for this one field type.
template <typename Instance>
bool json_value_to_meta(const entt::meta_type& field_type, const glz::generic& value, entt::meta_data& data, Instance& instance) {
    if (field_type == entt::resolve<int>()) {
        return data.set(instance, value.as<int>());
    }
    if (field_type == entt::resolve<float>()) {
        return data.set(instance, value.as<float>());
    }
    if (field_type == entt::resolve<bool>()) {
        return data.set(instance, value.get<bool>());
    }
    if (field_type == entt::resolve<double>()) {
        return data.set(instance, value.as<double>());
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

// The read-side counterpart of meta_component_to_json() above - completes the round-trip this
// module's own dependency comment names as Glaze's whole reason for being chosen
// (KonativeDependencies.cmake: "macro-free reflection-driven JSON, for config/hot-reload"). Parses
// `json` and sets every FIELD PRESENT IN IT onto the real `instance`, by name, driven entirely by
// entt::meta's own runtime data - matching Glaze's own documented "partial update" philosophy
// (glz::read_json into an existing object only touches fields present in the JSON; a field this
// component reflects but the JSON simply doesn't mention is left untouched, not treated as an
// error). Returns false on malformed JSON, an unresolvable field name, or a field whose value type
// detail::json_value_to_meta() doesn't handle.
//
// Templated on T (unlike meta_component_to_json()'s fully type-erased entt::meta_any parameter)
// for a real, verified reason, not an inconsistency: entt::meta_any's own implicit
// single-argument constructor COPY-constructs a new, owned value (confirmed directly against the
// vendored meta.hpp - it forwards through std::in_place_type<std::decay_t<Type>>, decaying away
// any reference), so a plain `entt::meta_any instance` parameter here would silently write through
// to a temporary copy, never reaching the caller's real object. entt::forward_as_meta(instance)
// does NOT fix this either - that's the exact mechanism pfr_auto_registration_self_check.hpp's own
// comment already documents as broken for meta_data::set()/get() specifically. A template
// parameter accepting the real T& directly, passed straight through to meta_data::set() (which
// converts it via meta_handle's own implicit Type& constructor, the one proven-correct path), is
// the actually-correct fix - not a stylistic choice.
template <typename T>
bool meta_component_from_json(entt::meta_type type, T& instance, const std::string& json) {
    if (!type) {
        return false;
    }

    glz::generic parsed{};
    if (glz::read_json(parsed, json)) {
        return false; // malformed JSON
    }

    for (auto [id, data] : type.data()) {
        (void)id;
        entt::meta_prop name_prop = data.prop(detail::kFieldNamePropId);
        if (!name_prop) {
            return false; // a data member entt::meta knows about but with no recorded real name
        }
        std::string field_name = name_prop.value().cast<std::string>();

        if (!parsed.contains(field_name)) {
            continue; // partial update - this field simply isn't in the JSON, leave it as-is
        }

        if (!detail::json_value_to_meta(data.type(), parsed[field_name], data, instance)) {
            return false;
        }
    }

    return true;
}

} // namespace konative::reflect
