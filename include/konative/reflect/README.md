# include/konative/reflect/

The `entt::meta` registration surface: how a C++ component type gets both reflected (queryable by
id/name at runtime) and made constructible-by-id into an `entt::registry` it wasn't compiled
knowing about.

## Hard rules

- **`entt::meta` reflection and registry-construction are two separate concerns that must be
  registered together.** `entt::registry` cannot construct a component pool from only a reflected
  `type_info`/hash — it needs the concrete C++ type at the call site
  (`ARCHITECTURE.md` §3, confirmed against EnTT's own maintainer). Every call to
  `reflect_component<T>()` in `meta_registry.hpp` registers BOTH the type id AND an
  emplace-into-registry thunk (`detail/registration_thunk.hpp`) — never add a reflection-only
  registration path that skips the thunk, or generic "add component by reflected id" tooling will
  silently fail for that type.
- **Reflect only what a consumer (editor tooling, serialization, scripting bridge) actually
  needs.** Each `.data<>()`/`.func<>()` call is its own template instantiation and its own
  compile-time cost (`ARCHITECTURE.md` §2/§3) — reflecting every field of every component "just in
  case" is a real, measurable build-time tax, not a free annotation.
- **`ReflectableComponent` (component_traits.hpp) is the gate.** A component that doesn't satisfy
  it (not default-constructible, not an aggregate) should not be reflected — fix the type or don't
  reflect it, don't relax the concept to fit a type that doesn't belong in generic tooling.
- **This module has no opinion on serialization formats.** `reflect_component<T>()` only wires
  `entt::meta`; pairing a reflected type with Glaze/cereal (`ARCHITECTURE.md` §4) is a separate,
  unvalidated-in-the-wild combination each consumer opts into explicitly, not something this
  module should assume or automate.

## Adding to this folder

A new file here should be either (a) a new registration helper generalizing
`meta_registry.hpp`'s pattern, or (b) a new concept in `component_traits.hpp` gating what's
reflectable. Actual component *type* definitions do not belong here — they live wherever their
owning system/module defines them (e.g. `ecs/` or a future gameplay module), with only the
`reflect_component<T>()` call referencing this module.
