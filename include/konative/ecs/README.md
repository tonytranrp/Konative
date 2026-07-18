# include/konative/ecs/

Thin wrappers over `entt::registry` (`Registry`/`Entity`/`kNullEntity`), an ordered
single-threaded `SystemGraph`, and `World` — the one-per-app-instance composition root (one
`Registry`, one `SystemGraph`, one event `Dispatcher`).

## Hard rules

- **Don't wrap what EnTT already does well.** `registry.hpp` is a deliberate one-line alias
  (`using Registry = entt::registry;`), not a wrapping class — see `ARCHITECTURE.md` §1's whole
  premise ("don't hand-roll what a good library already solves"). Only add indirection here if
  there's a real, concrete reason Konative code needs to intercept/extend `entt::registry`'s own
  behavior, not out of habit.
- **`entt::registry::ctx()` is Konative's dependency-injection mechanism — there is no other
  one.** Register cross-cutting services (renderer handles, asset managers, audio) via
  `world.registry().ctx().emplace<Service>(...)`. Do not introduce a second composition/DI
  mechanism anywhere in this module or any module that depends on it
  (`ARCHITECTURE.md` §4 explicitly rejected a dedicated DI library for this exact reason).
- **`SystemGraph` is intentionally single-threaded and dumb.** Parallel/cross-system-dependency
  scheduling is `scheduling/`'s job (Taskflow), layered on top via the
  `view.handle()`-index-splitting pattern (`ARCHITECTURE.md` §5) — do not add threading, futures,
  or a task graph directly into `SystemGraph`/`system.hpp`. If a system needs to run across
  multiple threads, that's expressed in `scheduling/`, called *from* a system, not built into the
  `System` concept itself.
- **`World::tick()` is the one place `Dispatcher::update()` gets called.** Don't call
  `dispatcher.update()` from anywhere else in the codebase — a second flush point breaks the
  "once per frame, in one place" guarantee `ARCHITECTURE.md` §5 relies on for reasoning about
  event delivery timing.

## Adding to this folder

A new file here should extend the registry/system/world composition itself (e.g. a new
`SystemGraph`-adjacent scheduling primitive, a new `World` accessor). Concrete gameplay
components and systems belong in whatever module owns that gameplay concern, not here — `ecs/` is
infrastructure, not content.
