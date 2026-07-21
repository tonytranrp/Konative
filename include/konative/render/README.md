# include/konative/render/

> **SUPERSEDED FOR RENDERING (confirmed 2026-07-18, not pending)**: this folder's content below
> describes rendering via Kotlin/Native + raw EGL — that design is confirmed superseded, not
> "in flight." Rendering is real, landed, verified-on-real-hardware JVM-hosted Jetpack Compose
> (dex-embedded, loaded via `InMemoryDexClassLoader`; `ARCHITECTURE.md` §6.6/§6.7). The hard rule
> "no EGL/GLES/Vulkan header here" still holds, now permanently rather than provisionally — nothing
> in this project renders via C++ or Kotlin/Native anymore, and no live code path currently
> constructs or calls the `Renderer` class below (confirmed: nothing under `src/` references
> `konative::render`). Treat `renderer.hpp` below as a frozen, historical description of a
> superseded design, not current guidance for new rendering work.

The C++ side of rendering, back when Kotlin/Native+EGL was the live design (see banner above for
current status) — after `ARCHITECTURE.md` §6.2's revision, deliberately just one small class
(`renderer.hpp`'s `Renderer`) translating window/tick events into three calls across the
Kotlin/Native interop boundary. Nothing else.

## Hard rules — the ones that matter most in this whole repo

- **No `EGL/*.h`, `GLES2/*.h`, `GLES3/*.h`, or `vulkan/*.h` header may ever be `#include`d
  anywhere under `include/konative/render/`, or anywhere else under `include/konative/`.**
  Rendering was historically owned entirely by Kotlin/Native (`native/src/Renderer.kt`), which
  cinterop-binds those headers directly — that whole path is now frozen/superseded (this file's own
  banner above), and real rendering is JVM-hosted Compose (`embedded_kotlin/`) instead, which needs
  none of these headers either. An earlier version of this skeleton had a real C++ GLES/Vulkan
  backend here (`render/backend/gles/`, `render/backend/vulkan/`) — it was deleted on purpose. If
  you're tempted to add one now "just to test something," don't: this rule holds permanently
  regardless of which rendering path is current.
- **`Renderer` may only ever grow by adding a new thin forwarding method plus a matching new
  `@CName` function on the Kotlin side** — never by adding real graphics logic in C++. If a method
  here does anything more than "take an event, call one interop function," that's a sign the logic
  belongs in Kotlin instead. (This rule holds permanently regardless of `render/`'s frozen status,
  same as bullet 1 above — it's about this module never growing real graphics logic, not about the
  now-frozen Kotlin/Native side still being where new logic would go in practice.)
- **This module depends on `interop/`, `events/`, and `core/` only.** It must never depend on
  `platform/android/` directly — keeping this direction one-way is what lets `Renderer` stay
  testable independent of whatever drives it on the platform side. (The concrete platform-side
  driver this rule protects against depending back has changed since this was written — the
  `android_native_app_glue` event loop it originally named was deleted in commit `3618fb5` in favor
  of a `JNI_OnLoad` entry point, `ARCHITECTURE.md` section 6.4 — and per this whole file's banner
  above, `render/`'s Kotlin/Native design is itself confirmed superseded, so this rule is kept for
  the general one-way-dependency principle alone, not because the specific driver it named, or the
  Kotlin/Native renderer it points at, is still the live architecture.)

## Adding to this folder

Only add a file here if it's genuinely about translating a *new kind* of platform event into the
interop boundary (e.g. a future `SurfaceResizedEvent` forward) for the frozen Kotlin/Native design
this module still forwards to. Anything about *how* a frame is actually drawn belongs in
`native/src/`, never here — though real, current rendering work almost certainly belongs in
`embedded_kotlin/` instead (Compose), not in this frozen module at all; see the banner above before
adding anything here.
