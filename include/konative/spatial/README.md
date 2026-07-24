# include/konative/spatial/

Real ECS-side spatial data - `Transform` (`transform.hpp`), the component `ARCHITECTURE.md`
section 4's dependency table names as GLM's actual reason for being chosen ("Math for ECS-side
transforms/components"), plus pure operations on it: `to_matrix()` (`transform.hpp`), `approach()`
(`approach.hpp`, frame-rate-independent exponential glide toward a target), and opt-in cereal
serialization (`transform_serialize.hpp` - a separate header so non-snapshotting consumers don't
inherit cereal's include graph). `ecs/glm_storage_self_check.hpp`'s own
`PackedTransformSelfCheckComponent` is a synthetic, self-check-only stand-in for GLM's
packed-vec3-through-EnTT-storage question - not a real, reusable component. This module is the
real one.

**Status**: landed, self-checked (`transform_self_check.hpp`, real EnTT storage round-trip +
hand-verified `to_matrix()` math), and - as of 2026-07-23 - **consumed by a real, shipping,
on-device-verified feature**: `jni_onload.cpp`'s follower-dot demo entity pairs `Transform` with a
`PointerFollow` target, a real System glides it via `approach()` every tick, touch input aims it,
the live position renders as a real Compose circle (`nativeGetFollowerX/Y`), and both components
ride the durable snapshot (the dot survives a process kill at its exact position - see
`ARCHITECTURE.md`'s status-table row for the verified evidence). The earlier "no consumer yet"
caveat this paragraph used to carry is closed.

## Hard rules

- **`Transform` stays a plain aggregate — position/rotation/scale only, no hierarchy.** No parent
  entity/component reference, no local-vs-world distinction, no dirty-flag/caching machinery.
  Nothing in this codebase has a real, current need for nested transforms - hierarchy is a real,
  separate design decision (world-space caching, dirty-flag propagation, cycle prevention) worth
  making when a real consumer actually needs it, matching this project's established "no driving
  need, don't build it speculatively" discipline (`ecs/registry.hpp`'s own `kNullEntity` is the
  same shape of restraint). Don't add hierarchy fields here without a real, concrete consumer
  driving the decision.
- **3D position/rotation/scale (`glm::vec3`/`glm::quat`/`glm::vec3`), not a narrower 2D-only
  shape.** This is the standard, conventional ECS Transform layout (Unity, Godot, Bevy all use
  exactly this) - even though this project's only current rendering surface is 2D Compose, nothing
  C++-side currently consumes `Transform` at all, so there's no real driving need forcing a
  narrower choice. 2D usage is still fully expressible (z as depth/layering, rotation constrained
  to the Z axis) without redesigning later if a real 3D need ever arrives.
- **`to_matrix()` is a free function, not a member.** Matches this codebase's established
  convention of keeping ECS components plain data (`HeartbeatCounter`, `AppConfig`) with behavior
  expressed separately, not object methods.
- **Never rely on `glm::quat{}`'s own default constructor for an identity value.** Confirmed
  against the real vendored `glm/detail/type_quat.hpp`: `qua()`'s default constructor is
  `GLM_DEFAULT` (compiler-generated, `= default`), which leaves its four floats genuinely
  uninitialized unless `GLM_FORCE_CTOR_INIT` is defined (this project doesn't define it anywhere).
  `Transform::rotation`'s default member initializer is explicit (`{1.0F, 0.0F, 0.0F, 0.0F}`,
  confirmed w-first against that same file's real `qua(T w, T x, T y, T z)` constructor) for
  exactly this reason - don't change it to rely on default-construction.

## Adding to this folder

Real spatial/gameplay components that need GLM math belong here (a `Velocity` component, a
`BoundingBox`, etc.) - but only once something in this codebase has a real, concrete need for one,
matching the same discipline `Transform` itself followed. A rendering or physics SYSTEM that reads
`Transform` (via `to_matrix()` or its raw fields) belongs in whatever module owns that concern
(future `render/`-successor or a dedicated `physics/`), not here - this folder is spatial DATA, not
systems that act on it, the same infrastructure-vs-content split `ecs/README.md` draws for the ECS
composition root itself.
