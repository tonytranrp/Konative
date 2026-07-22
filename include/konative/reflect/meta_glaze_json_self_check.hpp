#pragma once

#include <string>

#include <entt/entt.hpp>
#include <glaze/glaze.hpp>

#include "konative/reflect/meta_glaze_json.hpp"
#include "konative/reflect/pfr_auto_registration.hpp"

// A real, permanent self-check that entt::meta + Glaze compose correctly - ARCHITECTURE.md
// section 9's explicitly-flagged "genuinely unproven" pairing, the sibling item to
// pfr_auto_registration_self_check.hpp's own entt::meta + Boost.PFR self-check. Same "code checks
// itself" precedent as every other self-check in this codebase.
namespace konative::reflect {

namespace detail {

// Deliberately a DIFFERENT test-local component from PfrSelfCheckComponent (not reused) - this
// self-check exercises meta_component_to_json() specifically, and keeping it independent means a
// future change to one self-check's component shape can't silently affect the other's coverage.
struct GlazeSelfCheckComponent {
    int count = 0;
    float ratio = 0.0F;
    bool enabled = false;
};

// A real, GCC-specific compile error found via real CI (2026-07-22, never reproduced on this
// project's own Clang/Windows dev machine): Glaze's automatic reflection needs real EXTERNAL
// linkage for a type it reflects (glz::detail::external<T> internally) - a type defined LOCALLY
// inside a function body has no linkage, which GCC correctly rejects
// ("declared using local type ..., is used but never defined [-fpermissive]") and Clang silently
// accepted. Deliberately a SEPARATE shape from GlazeSelfCheckComponent above (not reused) - this
// one exists purely to prove the JSON text meta_component_to_json() produces is independently
// parseable, with zero relationship to reflect_component_auto<T>()/entt::meta.
struct ParsedShape {
    int count = 0;
    float ratio = 0.0F;
    bool enabled = false;
};

} // namespace detail

// Verifies, in order: (1) a component reflected via reflect_component_auto<T>() (the SAME real
// registration pfr_auto_registration_self_check.hpp already proves) serializes to a real JSON
// object string with the right keys and values, driven entirely by entt::meta's own runtime data -
// this function never mentions GlazeSelfCheckComponent's field names as C++ identifiers; (2) the
// resulting JSON string round-trips through Glaze's own glz::read_json() back into real,
// concrete C++ values that match what was originally set.
inline bool run_meta_glaze_json_self_check() {
    using namespace entt::literals;
    constexpr entt::id_type kId = entt::hashed_string{"konative::reflect::GlazeSelfCheckComponent"};
    reflect_component_auto<detail::GlazeSelfCheckComponent>(kId);

    detail::GlazeSelfCheckComponent instance{};
    instance.count = 7;
    instance.ratio = 2.5F;
    instance.enabled = true;

    auto type = entt::resolve(kId);
    if (!type) {
        return false;
    }

    std::string json = meta_component_to_json(type, instance);
    if (json.empty()) {
        return false;
    }

    // Real Glaze parsing of the string meta_component_to_json() produced, back into a plain,
    // ordinary C++ struct it has zero relationship to reflect_component_auto<T>()/entt::meta for -
    // confirming the JSON text itself is genuinely well-formed and semantically correct, not just
    // non-empty.
    detail::ParsedShape parsed{};
    if (glz::read_json(parsed, json)) {
        return false; // a real Glaze parse error - the produced JSON was not valid/matching
    }

    return parsed.count == 7 && parsed.ratio == 2.5F && parsed.enabled == true;
}

} // namespace konative::reflect
