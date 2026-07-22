# include/konative/reflect/

The `entt::meta` registration surface: how a C++ component type gets both reflected (queryable by
id/name at runtime) and made constructible-by-id into an `entt::registry` it wasn't compiled
knowing about.

## Hard rules

- **`entt::meta` reflection and registry-construction are two separate concerns that must be
  registered together.** `entt::registry` cannot construct a component pool from only a reflected
  `type_info`/hash — it needs the concrete C++ type at the call site
  (`ARCHITECTURE.md` §3, confirmed against EnTT's own maintainer). Every call to
  `reflect_component<T>()` in `meta_registry.hpp` — and every call to `reflect_component_auto<T>()`
  in `pfr_auto_registration.hpp` (2026-07-22), the PFR-driven field-auto-registration sibling that
  registers the SAME thunk plus every field, without a consumer hand-writing `.data<>()` per field —
  registers BOTH the type id AND an emplace-into-registry thunk (`detail/registration_thunk.hpp`) —
  never add a reflection-only registration path that skips the thunk, or generic "add component by
  reflected id" tooling will silently fail for that type.
- **Reflect only what a consumer (editor tooling, serialization, scripting bridge) actually
  needs.** Each `.data<>()`/`.func<>()` call is its own template instantiation and its own
  compile-time cost (`ARCHITECTURE.md` §2/§3) — reflecting every field of every component "just in
  case" is a real, measurable build-time tax, not a free annotation. `reflect_component_auto<T>()`
  does reflect every field by design (that's its whole point) — reach for it when a component's
  fields genuinely all belong in generic tooling, and prefer hand-written `reflect_component<T>()`
  + scoped `.data<>()` calls when only some do.
- **`ReflectableComponent` (component_traits.hpp) is the gate.** A component that doesn't satisfy
  it (not default-constructible, not an aggregate) should not be reflected — fix the type or don't
  reflect it, don't relax the concept to fit a type that doesn't belong in generic tooling.
- **This module has one, specific, landed serialization capability — not a general opinion on
  serialization formats.** `meta_glaze_json.hpp`'s `meta_component_to_json()` (2026-07-22) pairs a
  reflected type with Glaze, but ONLY because it's driven entirely by `entt::meta`'s own runtime
  registration data (the `kFieldNamePropId` property `reflect_component_auto<T>()` attaches, plus
  `type.data()` enumeration) — it is not a generic "bring your own serialization format" mechanism,
  and it currently only understands `int`/`float`/`bool`/`double` field values
  (`meta_glaze_json.hpp`'s own `meta_value_to_json()`), returning an empty string for the whole
  object if any field doesn't fit. Other pairings (e.g. cereal, which already has its own separate,
  unrelated mechanism via `entt::snapshot` — see `ecs/registry_snapshot_self_check.hpp`) stay out of
  scope for this module; don't assume a second serialization format belongs here just because Glaze
  does.

## Adding to this folder

A new file here should be either (a) a new registration helper generalizing
`meta_registry.hpp`'s pattern (like `pfr_auto_registration.hpp`), (b) a new concept in
`component_traits.hpp` gating what's reflectable, or (c) a reflection-driven helper built entirely
on `entt::meta`'s own runtime registration data (like `meta_glaze_json.hpp`) — not a new
serialization format a consumer would opt into independently of what's already reflected here.
Actual component *type* definitions do not belong here — they live wherever their owning
system/module defines them (e.g. `ecs/` or a future gameplay module), with only the
`reflect_component<T>()`/`reflect_component_auto<T>()` call referencing this module.
