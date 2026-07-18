# include/konative/render/

> **PENDING REWORK (2026-07-17)**: this folder's content below describes rendering via
> Kotlin/Native + raw EGL, which `ARCHITECTURE.md`'s status banner marks superseded — rendering
> is moving to JVM-hosted Jetpack Compose (dex-embedded, loaded via `InMemoryDexClassLoader`).
> The hard rule "no EGL/GLES/Vulkan header here" likely still holds in spirit (rendering still
> shouldn't be reimplemented in C++), but this folder's actual contents will change once the
> in-flight Compose research lands — see `project-konative-autonomous-loop` memory. Don't treat
> `renderer.hpp` below as current until this banner is removed.

The C++ side of rendering — which, after `ARCHITECTURE.md` §6.2's revision, is deliberately just
one small class (`renderer.hpp`'s `Renderer`) translating window/tick events into three calls
across the Kotlin/Native interop boundary. Nothing else.

## Hard rules — the ones that matter most in this whole repo

- **No `EGL/*.h`, `GLES2/*.h`, `GLES3/*.h`, or `vulkan/*.h` header may ever be `#include`d
  anywhere under `include/konative/render/`, or anywhere else under `include/konative/`.**
  Rendering is owned entirely by Kotlin/Native (`native/src/Renderer.kt`), which cinterop-binds
  those headers directly. An earlier version of this skeleton had a real C++ GLES/Vulkan backend
  here (`render/backend/gles/`, `render/backend/vulkan/`) — it was deleted on purpose. If you're
  tempted to re-add one "just to test something," don't: fix or debug the Kotlin/Native side
  instead, or extend the three-call interop surface (`interop/kotlin_native_bridge.hpp`) if a
  genuinely new capability is needed.
- **`Renderer` may only ever grow by adding a new thin forwarding method plus a matching new
  `@CName` function on the Kotlin side** — never by adding real graphics logic in C++. If a method
  here does anything more than "take an event, call one interop function," that's a sign the logic
  belongs in Kotlin instead.
- **This module depends on `interop/`, `events/`, and `core/` only.** It must never depend on
  `platform/android/` directly — keeping this direction one-way is what lets `Renderer` stay
  testable independent of whatever drives it on the platform side. (The concrete platform-side
  driver this rule protects against depending back has changed since this was written — the
  `android_native_app_glue` event loop it originally named was deleted in commit `3618fb5` in favor
  of a `JNI_OnLoad` entry point, `ARCHITECTURE.md` section 6.4 — but per this whole file's own
  pending-rework banner above, `render/`'s Kotlin/Native design is itself superseded, so this rule
  is being kept for the general one-way-dependency principle, not because the specific old driver
  it named is still accurate.)

## Adding to this folder

Only add a file here if it's genuinely about translating a *new kind* of platform event into the
interop boundary (e.g. a future `SurfaceResizedEvent` forward). Anything about *how* a frame is
actually drawn belongs in `native/src/`, never here.
