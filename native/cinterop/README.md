# native/cinterop/

**Empty by default, and that's correct.** Kotlin/Native ships pre-built cinterop bindings for EGL,
GLES2, GLES3, and core Android NDK types (`ANativeWindow`, `ANativeActivity`, JNI types) for every
`androidNative*` target already, as `platform.egl` / `platform.gles2` / `platform.gles3` /
`platform.android` — sourced from `kotlin-native/platformLibs/src/platform/android/*.def` in the
JetBrains/kotlin compiler distribution itself. `native/src/Renderer.kt` just `import`s them
directly. **Do not write a `.def` file for EGL, GLES, or `ANativeWindow`/`ANativeActivity` — it
already exists inside the Kotlin/Native distribution you're compiling against.**

## When a `.def` file genuinely belongs here

Only for a C API Kotlin/Native does **not** already bundle for Android — the one confirmed
example is **Vulkan** (`vulkan/vulkan.h`, `vulkan/vulkan_android.h`): no `vulkan.def` ships in
Kotlin/Native's `platformLibs`, so binding it requires a hand-authored `.def` with explicit NDK
sysroot `compilerOpts`, following the same grammar every other `.def` file uses
(`headers`/`headerFilter`/`compilerOpts`/`linkerOpts`/`package`, with `.{target}`-suffixed
per-ABI variants — see any real `.def` file, e.g. Kotlin/Native's own bundled `egl.def`, for the
grammar shape). **No such file has been written or verified yet** — Vulkan-from-Kotlin/Native-on-
Android has essentially no public prior art (`ARCHITECTURE.md` §6.2/§9). If you're adding one,
treat it as a from-scratch spike, not an adaptation of an existing known-good file.

## Adding a `.def` file here

1. Confirm the API genuinely isn't already in `platform.*` first (check
   `kotlin-native/platformLibs/src/platform/android/` in the JetBrains/kotlin source tree for your
   pinned Kotlin/Native version).
2. Wire it into `native/CMakeLists.txt`'s `konative_add_kotlin_native_module(... CINTEROP_DEF
   <file>)` argument.
3. Verify the resulting Kotlin bindings actually compile and link before writing any code against
   them — this is exactly the kind of unproven interop surface `ARCHITECTURE.md` §9 asks you to
   spike first.
