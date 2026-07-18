# embedded_kotlin/

The real Kotlin+Compose source that gets compiled, dexed, and embedded into `konative_app_native`'s
`.so` (`ARCHITECTURE.md` section 6.6) — **not** built by Gradle, **not** part of `testapp/`. This is
the actual functional example the whole framework exists to produce: everything past
`src/platform/android/jni_onload.cpp`'s one `CallStaticVoidMethod` handoff runs from here.

## Hard rules

- **Never built by Gradle/AGP.** Compiled by Konative's own `kotlinc`+Compose-compiler-plugin+`d8`
  CMake pipeline (see `cmake/modules/` once that pipeline lands — as of this file's writing it is
  still a hand-run scratchpad recipe, not yet automated; see `ARCHITECTURE.md` section 6.7's status
  table). `testapp/`'s Gradle build never sees these `.kt` files at all — that module owns exactly
  one file, `testapp/app/src/main/java/com/konative/testapp/MainActivity.kt`, and nothing here may
  ever be added to it.
- **`com.konative.generated.KonativeEntryPoint`'s `@JvmStatic install(Application)` is the ONLY
  contract `jni_onload.cpp` depends on** (exact signature:
  `(Landroid/app/Application;)V`, looked up via `GetStaticMethodID`/`CallStaticVoidMethod`). Don't
  rename the class, package, or method without updating `jni_onload.cpp`'s `kEntryPointClass`
  constant and this contract note together, in the same commit.
- **No further JNI/reflection past the one `install(Application)` handoff.** Everything here is real
  compiled Kotlin — `Application.ActivityLifecycleCallbacks`, manual `LifecycleOwner`/
  `ViewModelStoreOwner`/`SavedStateRegistryOwner` wiring (`ComposeHostOwner`), `ComposeView` —
  matching `research/jni_activity_bootstrap_research.md` section 3.3's "favor real Kotlin over
  reflection" conclusion. If a new capability seems to need a reflective call from C++, it almost
  certainly belongs as ordinary Kotlin here instead.
- **This module's own dependency set (Compose runtime/ui/foundation, lifecycle-runtime,
  lifecycle-viewmodel, savedstate, kotlinx-coroutines) must be bundled into the dex blob itself** —
  none of it is part of the Android OS image the way `View`/`Dialog`/`WindowManager` are (unlike
  `GameHub`'s simpler Dialog-based dex blobs). **Deliberately does NOT depend on
  `androidx.compose.material3`** — a full Material You design system (dynamic color, ripple,
  typography scale) is a lot of embedded-blob weight this framework's own trivial proof-of-concept
  doesn't need; use `androidx.compose.foundation.text.BasicText` + a manual `TextStyle` instead of
  `material3.Text`/`MaterialTheme`. A real app embedding this framework can add material3 back —
  just be aware of the real, measured size cost (see Status below).

## Status (2026-07-17) — real, on-device-attempted, not yet fully working

`KonativeEntryPoint.kt` compiles cleanly against the real, Gradle-resolved AndroidX dependency
closure (see `r_shim/README.md` for how that closure was determined and why `r_shim/` exists) and
R8-shrinks to a **single ~1.2-2.4MB `classes.dex`** (down from ~20MB/multidex unshrunk — this
answers `research/jni_activity_bootstrap_research.md` section 5.3's previously-unmeasured
embedded-blob-size risk: it's real, but shrinkable to something that fits Konative's current
single-dex-buffer `load_class_from_dex()` comfortably, once Material3 is left out and R8 shrinking
is used). Two real bugs were found and fixed by actually compiling/running this, not by review:

1. `performRestore()`/`performSave()` belong on `SavedStateRegistryController`, not
   `LifecycleRegistry` — `research/jni_activity_bootstrap_research.md` section 5.2's own reference
   sketch had this wrong (it was never actually compiled before being written up).
2. R8's default `--release` obfuscation (`-dontobfuscate` now set) made an on-device crash's error
   message unreadable (`NoSuchFieldError` on a renamed one-letter class) — turning it off doesn't
   fix anything by itself, but is necessary to read what's actually wrong. Also needed
   `-dontoptimize` (a real, reproduced-twice `NoSuchMethodError` on `kotlin.collections.ArraysKt
   .fill$default` inside `androidx.collection.MutableScatterMap`, surviving a `kotlin-stdlib`
   version swap unchanged, went away only with R8's optimizer off — looks like an R8
   optimizer bug/mismatch with this specific call shape, not investigated further; see
   `combined_proguard.pro`-equivalent notes for exact repro details if reproducing this build by
   hand).

**Current, real, on-device blocker, precisely diagnosed, not yet solved**: `ComposeView`
initialization unconditionally constructs a `FontFamilyResolver` (`androidx.compose.ui.text.font
.FontFamilyResolverImpl`), which touches `kotlinx.coroutines.Dispatchers.Main`. Both of
`kotlinx-coroutines`'s Main-dispatcher-discovery paths (`FastServiceLoader` and the plain
`java.util.ServiceLoader` fallback — confirmed by decompiling `kotlinx.coroutines.internal
.MainDispatcherLoader` directly) need classloader **resource** lookup
(`META-INF/services/kotlinx.coroutines.internal.MainDispatcherFactory`), not just class lookup.
`dalvik.system.InMemoryDexClassLoader` loads compiled bytecode from a raw byte buffer with **no
JAR/ZIP resource backing at all** — there is no mechanism for `ClassLoader.getResource(...)` to
find anything for a dex-blob-loaded class, regardless of R8 settings. Real on-device result:
`java.lang.IllegalStateException: Module with the Main dispatcher is missing`, thrown from
`MainDispatcherLoader.<clinit>`, crashing `ComposeView` attachment.

**This is an architecture-level gap in the loader mechanism itself, not something fixable from
Kotlin source or `r8` flags.** The real fix is one of:
- Extend `konative::jni::load_class_from_dex()`/the embed mechanism to ALSO serve
  `META-INF/services/*`-style resource lookups (e.g. a custom `ClassLoader` subclass wrapping
  `InMemoryDexClassLoader` that answers `findResource`/`findResources` from a small resource table
  embedded alongside the dex bytes) — the architecturally "real" fix, matching how a real APK's
  own classloader already does this.
- Avoid any AndroidX/Kotlin library that relies on `ServiceLoader`-style discovery in the embedded
  module entirely (fragile long-term — this could recur with other libraries, not just
  `kotlinx-coroutines`, and Compose's `FontFamilyResolver` can't be avoided from application code).

Not yet attempted; flagged here precisely so a future iteration doesn't have to
re-diagnose this from scratch.
- **One flat `src/com/konative/generated/` package tree**, matching the fixed
  `com.konative.generated` package `jni_onload.cpp` looks up by name — don't introduce a deeper
  package hierarchy without a real reason (unlike `include/konative/**`'s deep C++ folder nesting
  convention, there's no equivalent reason for depth here: this is one small, tightly-coupled
  module, not a large multi-domain library).

## Adding to this folder

New `@Composable` UI, new `ActivityLifecycleCallbacks` behavior, new state — all real Kotlin here,
following ordinary Kotlin/Compose idioms (this folder is not bound by
`feedback-konative-coding-style`'s C++-specific `.hpp`-first/EnTT-metaprogramming rules, which apply
to the C++ core in `include/konative/**` and `src/**`, not to this JVM Kotlin module). Keep
`ComposeHostOwner` and `KonativeEntryPoint` together in one file, matching
`research/jni_activity_bootstrap_research.md` section 5.2's validated reference design, unless a
real second consumer of `ComposeHostOwner` justifies splitting it out.
