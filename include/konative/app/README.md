# include/konative/app/

The `Application` base class (owns the one-per-process `ecs::World`) and the `create_application()`
entry-point contract every Konative app implements exactly once. Also the application-level config
surface: `app_config.hpp` (the reflected `AppConfig` component `jni_onload.cpp` stores in
`registry().ctx()`, plus `clamp_to_valid()` - see its own comment for the real division-by-zero
reason it exists) and `config/json_config_file.hpp` (`JsonConfigFile<T>`, the file-backed
provision/load/hot-reload mechanism behind it - the real "config/hot-reload" purpose
`KonativeDependencies.cmake` names for Glaze; see `ARCHITECTURE.md` §9's write-up).

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
  to drive an `Application` without linking any Android-specific code at all. This is a strictly
  one-way rule — the DEPENDENCY DIRECTION only ever runs from `platform/android/` toward `app/`,
  never the reverse: **`src/platform/android/jni_onload.cpp` DOES depend on `app/` for real**, since
  2026-07-21 — its `KonativeAndroidApp` is the one real `create_application()` implementation for
  that target (`entry_point.hpp`'s own contract), and `KonativeEntryPoint.kt`'s
  `ActivityLifecycleCallbacks`/`Choreographer.FrameCallback` call back into it via `RegisterNatives`.
  That's `app/` being DEPENDED ON, which this Hard Rule was never about forbidding — only `app/`
  itself `#include`-ing anything under `platform/android/`/`render/` would violate it, and that
  still doesn't happen anywhere.

## Adding to this folder

A new file here should extend the application composition-root contract itself (a new lifecycle
event bridge, a new `Application` accessor). A specific app's behavior is a subclass living in
that app's own source (`examples/*/main.cpp`, `testapp/`'s eventual native logic), never a new
file added directly to this folder.
