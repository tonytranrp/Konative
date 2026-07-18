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
  platform-agnostic on purpose. A desktop test harness (or a future second platform) should be able
  to drive an `Application` without linking any Android-specific code at all.
  **Current status, honestly**: `src/platform/android/jni_onload.cpp` (the real `JNI_OnLoad` entry
  point, `ARCHITECTURE.md` section 6.4) does not depend on `app/` at all right now — the earlier
  `android_native_app_glue` design that DID call into `app/`'s `entry_point.hpp`/
  `detail/lifecycle_bridge.hpp` was deleted (commit `3618fb5`), and the new `JNI_OnLoad`-based
  design's relationship to this module's `Application`/ECS lifecycle is a real open item, not yet
  decided — see `entry_point.hpp`/`detail/lifecycle_bridge.hpp`'s own comments. The one-way
  dependency rule above still holds (this module still must never depend the other way), it's just
  that nothing currently depends on this module from the Android platform side either.

## Adding to this folder

A new file here should extend the application composition-root contract itself (a new lifecycle
event bridge, a new `Application` accessor). A specific app's behavior is a subclass living in
that app's own source (`examples/*/main.cpp`, `testapp/`'s eventual native logic), never a new
file added directly to this folder.
