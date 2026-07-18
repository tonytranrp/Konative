# include/konative/platform/android/

The `android_native_app_glue`/`GameActivity` event-loop wiring: the `NativeAppHandle` alias,
`APP_CMD_*` translation into `konative::app::Application` calls, and the `ALooper` pump.

## Hard rules

- **This module translates platform lifecycle into `konative::app`'s platform-agnostic
  vocabulary — it must never be where actual application or rendering logic lives.**
  `detail/app_cmd_translation.hpp`'s `handle_app_cmd()` should stay a small, boring switch mapping
  `AppCmd` values onto `Application::start()/resume()/pause()/destroy()` calls — if you find
  yourself adding real logic inside that switch, it belongs in an `Application` override instead.
- **Window lifecycle (`APP_CMD_INIT_WINDOW`/`APP_CMD_TERM_WINDOW`) goes to
  `konative::render::Renderer`, not through `handle_app_cmd()`.** `Renderer::on_window_created`/
  `on_window_destroyed` forward straight into the Kotlin/Native interop boundary
  (`ARCHITECTURE.md` §6.2) — this module's own switch (`detail/app_cmd_translation.hpp`)
  deliberately has a `default: break;` for those two commands rather than duplicating that
  dispatch.
- **`NativeActivity` vs. `GameActivity` is a build-time choice made in exactly one place**
  (`src/platform/android/CMakeLists.txt`'s glue-library choice, matched by
  `testapp/app/src/main/AndroidManifest.xml`'s activity declaration). If that choice is ever
  flipped, both sides must change together — never let this module silently assume one glue
  library while the manifest declares the other.
- **`native_app_glue.hpp` only forward-declares `struct android_app`.** It exists so the rest of
  this module's public headers can name `NativeAppHandle` without pulling in the full NDK glue
  header — the real `#include <android_native_app_glue.h>` only belongs in the `.cpp`
  implementation files under `src/platform/android/`.

## Adding to this folder

A new file here should be new glue-layer plumbing (a new lifecycle translation, a new looper
identifier handled). If you're adding something that reacts to a lifecycle event rather than
translating one, that reaction belongs in `konative::app::Application` or
`konative::render::Renderer`, called *from* here, not implemented here.
