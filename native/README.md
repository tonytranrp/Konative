# native/

> **SUPERSEDED FOR RENDERING (confirmed 2026-07-18, not pending)**: rendering moved to JVM-hosted
> Jetpack Compose, dex-embedded and loaded via `InMemoryDexClassLoader` — this has landed and been
> verified end-to-end on real hardware (`ARCHITECTURE.md` §6.6/§6.7/§13), not just proposed. The
> Kotlin/Native-AOT-compiled-EGL approach this folder documents/contains (`src/Renderer.kt`) is
> fully superseded and frozen — kept only as a historical record, not "pending deletion once the
> replacement is built": the replacement has been built, shipped, and re-verified multiple times
> since this note was first written. Do not extend this folder for rendering; see
> `project-konative-autonomous-loop` memory and `ARCHITECTURE.md` §6.7 for current status. Kotlin/
> Native itself may still be useful for non-rendering native logic later — the C-ABI mechanics
> documented below aren't wrong, just not what owns rendering anymore.

The Kotlin/Native side — compiled ahead-of-time to real ARM64/x86_64 machine code by
`kotlinc-native` (never the JVM-targeting `kotlinc`), linked directly into the fused `.so`.
**Historically** owned all rendering (EGL/GLES/Vulkan); that responsibility now belongs entirely to
`embedded_kotlin/`'s JVM-hosted Compose UI (`ARCHITECTURE.md` §6.6/§6.7). This folder is frozen for
rendering purposes — it exists now only as a historical record plus a real, documented C-ABI
mechanism available for a future non-rendering need.

## Hard rules

- **This folder is frozen for rendering — do not extend it for that purpose.** It used to be the
  only place EGL/GLES/Vulkan calls could exist in the framework; rendering is now JVM-hosted Compose
  and zero OpenGL/EGL/Vulkan headers appear anywhere in the live rendering path (`ARCHITECTURE.md`
  §6.7). `include/konative/render/` (C++) documents the same supersession — see its own `README.md`.
  If you're about to write a new `gl*`/`egl*`/`vk*` call anywhere, stop: that's not this project's
  rendering path anymore.
- **Every function the C++ core calls must be top-level and `@CName`-annotated**, exporting a flat
  C symbol matching a forward declaration in `include/konative/interop/kotlin_native_bridge.hpp`
  exactly (`ARCHITECTURE.md` §6.3). Never rely on the generated `_api.h` nested-struct surface.
- **EGL, GLES2/GLES3, and core Android NDK types (`ANativeWindow`, `ANativeActivity`) need NO
  custom cinterop `.def` file at all** — Kotlin/Native ships pre-built bindings for all of them,
  for every `androidNative*` target, as `platform.egl`/`platform.gles2`/`platform.gles3`/
  `platform.android` (confirmed against `kotlin-native/platformLibs/src/platform/android/*.def`
  in the JetBrains/kotlin source tree). Just `import platform.egl.*` etc. directly — see
  `native/src/Renderer.kt`. `native/cinterop/*.def` files are ONLY for a genuinely un-bundled C
  API (the confirmed example is Vulkan — see `native/cinterop/README.md`); never hand-declare an
  NDK C function's signature directly in Kotlin instead of either import path.
- **An `ANativeWindow*` (or any other raw pointer) crossing the `@CName` boundary from C++ arrives
  as a `COpaquePointer?` — always null-check it, then `.reinterpret<T>()` to the cinterop-bound
  type before using it, never assume it's non-null.** This exact pattern (pointer handoff across
  the flat C-ABI boundary, then `reinterpret` on the Kotlin side) is the framework's single least
  proven mechanism (`ARCHITECTURE.md` §9) — treat every new use of it as something to verify on
  the connected test device via `testapp/`, not something to assume works by analogy to the last
  place it was used.
- **Pin `kotlinc-native`'s version together with the NDK version `KonativeAndroidToolchain.cmake`
  resolves.** The documented libc++/symbol-conflict risk (`ARCHITECTURE.md` §6.3) is sensitive to
  this pairing — don't bump one without deliberately checking the other still links.

## Adding to this folder

A new `.kt` file here should own one coherent piece of native-side logic (e.g. a new
`Renderer.kt`-adjacent file for a second rendering concern, or a first piece of Kotlin-side
gameplay logic once the interop boundary is proven). Add the matching `@CName` export +
`extern "C"` C++ declaration in the same change, never one without the other.
