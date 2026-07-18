# include/konative/app/

The `Application` base class (owns the one-per-process `ecs::World`) and the `create_application()`
entry-point contract every Konative app implements exactly once.

## Hard rules

- **`create_application()` is implemented exactly once per binary, by the application author, not
  by this module.** `entry_point.hpp` only declares it — `include/konative/app/` must never
  provide a default/example implementation that could accidentally satisfy the linker for a real
  app that forgot to implement its own (a silent wrong-default is worse than a link error here).
- **`Application`'s virtual `on_*` methods are the only extension point.** A real app subclasses
  `Application` and overrides `on_started()`/`on_tick()`/etc. — it does not reach into
  `World`/`Registry` from outside an `Application` subclass. Keep this the single, obvious seam
  for "where does my app's code go."
- **Lifecycle event dispatch (`detail/lifecycle_bridge.hpp`) always fires before the corresponding
  virtual `on_*()` override.** This ordering is deliberate (systems that only care about the
  generic `AppStartedEvent` etc. shouldn't need to know about a specific `Application` subclass to
  react to it) — don't reorder it, and don't skip the event dispatch even if a given app's
  override doesn't need it.
- **This module must not depend on `platform/android/` or `render/`.** `Application` is
  platform-agnostic on purpose — `platform/android/` depends on `app/`, never the reverse. A
  desktop test harness (or a future second platform) should be able to drive an `Application`
  without linking any Android-specific code at all.

## Adding to this folder

A new file here should extend the application composition-root contract itself (a new lifecycle
event bridge, a new `Application` accessor). A specific app's behavior is a subclass living in
that app's own source (`examples/*/main.cpp`, `testapp/`'s eventual native logic), never a new
file added directly to this folder.
