# embedded_kotlin/

The real Kotlin+Compose source that gets compiled, dexed, and embedded into `konative_app_native`'s
`.so` (`ARCHITECTURE.md` section 6.6) — **not** built by Gradle, **not** part of `testapp/`. This is
the actual functional example the whole framework exists to produce: everything past
`src/platform/android/jni_onload.cpp`'s one `CallStaticVoidMethod` handoff runs from here.

**This is real, working, and verified on real hardware** — see Status below for the actual
screenshot proof, not just a compile-clean claim.

## Hard rules

- **Never built by Gradle/AGP directly.** Compiled by Konative's own `kotlinc`+Compose-compiler-
  plugin+`r8` pipeline, automated via `cmake/modules/KonativeEmbedKotlinDex.cmake`'s
  `konative_embed_kotlin_dex()` (see `ARCHITECTURE.md` section 6.6/6.7's status table) —
  `testapp/`'s Gradle build only *drives* this indirectly through `externalNativeBuild` invoking
  the same root `CMakeLists.txt`, it never compiles these `.kt` files with its own Kotlin Gradle
  plugin. `testapp/` itself owns exactly one file,
  `testapp/app/src/main/java/com/konative/testapp/MainActivity.kt`, and nothing here may ever be
  added to it.
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
- **One flat `src/com/konative/generated/` package tree**, matching the fixed
  `com.konative.generated` package `jni_onload.cpp` looks up by name — don't introduce a deeper
  package hierarchy without a real reason (unlike `include/konative/**`'s deep C++ folder nesting
  convention, there's no equivalent reason for depth here: this is one small, tightly-coupled
  module, not a large multi-domain library).
- **`r8-rules.pro` (this folder) must be passed to `r8` via `--pg-conf` on every real build.**
  Every rule in it exists because of a specific, real, on-device-reproduced failure (see that
  file's own comments) — this is not a generic boilerplate proguard file, and skipping it will
  reproduce bugs this project has already found and fixed once.
- **`r_shim/` is a stopgap, not a design** — see that folder's own `README.md`. Don't add a new
  file there without first confirming, by decompiling the real referencing bytecode
  (`javap` against the actual dependency jar), which exact fields are needed. Guessing field names
  or values here has real failure modes (see Status below for how many rounds this took even when
  done carefully).

## Status (2026-07-17) — real Compose UI, rendering, verified on real hardware

**Real, on-device, screenshotted proof**: a green `Box` filling the screen with white "Konative"
text — `KonativeRootComposable()`'s exact, real output — rendered on the rooted LDPlayer x86_64
emulator via `com.konative.testapp`. This is the full chain working end to end: `System
.loadLibrary()` → `JNI_OnLoad` → `verify_blob()` (SHA-256 self-check) → `load_class_from_dex()` →
`upgrade_to_resource_aware_loader()` (`KonativeResourceProvider`, see below) →
`KonativeEntryPoint.install(Application)` → `ActivityLifecycleCallbacks.onActivityCreated()` →
`ComposeView` construction → real Jetpack Compose composition → real rendering — no OpenGL/EGL/
Vulkan anywhere, exactly per this project's original design intent.

`KonativeEntryPoint.kt` compiles cleanly against the real, Gradle-resolved AndroidX dependency
closure (see `r_shim/README.md` for how that closure was determined and why `r_shim/` exists) and
R8-shrinks to a **single ~1.2-2.4MB `classes.dex`** (down from ~20MB/multidex unshrunk — this
answers `research/jni_activity_bootstrap_research.md` section 5.3's previously-unmeasured
embedded-blob-size risk: it's real, but shrinkable to something that fits Konative's current
single-dex-buffer `load_class_from_dex()` comfortably, once Material3 is left out and R8 shrinking
is used).

**Real bugs found and fixed by actually compiling/running this, not by review** (roughly in the
order they were hit — each one was a real, reproduced on-device failure, not a hypothetical):

1. `performRestore()`/`performSave()` belong on `SavedStateRegistryController`, not
   `LifecycleRegistry` — `research/jni_activity_bootstrap_research.md` section 5.2's own reference
   sketch had this wrong (it was never actually compiled before being written up).
2. R8's default `--release` obfuscation made an on-device crash's error message unreadable
   (`NoSuchFieldError` on a renamed one-letter class) — `-dontobfuscate` doesn't fix anything by
   itself, but is necessary to read what's actually wrong (see `r8-rules.pro`).
3. `~7` genuinely missing `R$id`/`R$string` classes (no AAPT2 step in this hand-rolled pipeline) —
   `r_shim/`, each field verified against real decompiled bytecode, not guessed.
4. A real, reproduced-twice `NoSuchMethodError` on `kotlin.collections.ArraysKt.fill$default`
   inside `androidx.collection.MutableScatterMap`, surviving a `kotlin-stdlib` version swap
   unchanged — an R8 optimizer bug/mismatch with this specific call shape, worked around with
   `-dontoptimize` (not root-caused further; see `r8-rules.pro`).
5. **The `Dispatchers.Main` blocker** (previously the last known blocker, now solved) — two
   distinct causes, both real, both required for the fix:
   - `dalvik.system.InMemoryDexClassLoader` loads bytecode from a raw byte buffer with no JAR/ZIP
     resource backing, so `ClassLoader.getResource()`/`getResourceAsStream()` always return
     nothing for a dex-loaded class. Fixed architecturally: `KonativeResourceProvider` (a plain
     `ClassLoader`, NOT an `InMemoryDexClassLoader` subclass — that class is `final`, a real
     compile-time-verified constraint an earlier draft hit directly) is used as a SECOND
     `InMemoryDexClassLoader`'s parent, so `ClassLoader.getResource()`'s standard parent-first
     delegation finds this provider's synthetic `META-INF/services/*` entries before falling
     through to the (always-empty) dex-based lookup. `src/platform/android/jni_onload.cpp`'s
     `load_class_from_dex()` bootstraps this opportunistically - see `dex_loader.hpp`'s
     `detail::upgrade_to_resource_aware_loader()`.
   - **Correction to this doc's own earlier claim**: it previously said *both* of
     `kotlinx-coroutines`'s Main-dispatcher-discovery paths need resource lookup. An independent
     verification pass decompiled `FastServiceLoader.loadMainDispatcherFactory$kotlinx_coroutines_core()`
     further than the original diagnosis had and found that on a real Android device
     (`ANDROID_DETECTED == true`, always true here), the "fast" path uses a hardcoded
     `Class.forName("kotlinx.coroutines.android.AndroidDispatcherFactory", ...)` — a plain class
     lookup needing no resources at all, only for the class to actually be **present**. It wasn't:
     R8 had silently shrunk away the entire `kotlinx.coroutines.android` package (and, separately,
     `KonativeResourceProvider` itself) since neither is referenced anywhere in the static call
     graph — only reachable via reflection, exactly like `KonativeEntryPoint.install()` already
     was. Both needed explicit `-keep` rules (`r8-rules.pro`) - the resource-provider fix alone did
     not solve the crash; the keep rules alone (tested independently) also did not solve it. Both
     were required together for the confirmed-working combination.

No known blockers remain for this proof-of-concept's scope. Real, still-open, lower-priority items:
the R8 optimizer bug in item 4 above (worked around, not root-caused), and `r_shim/`'s own stated
stopgap status (a real AAPT2 step would remove the need for it). The CMake automation for this
whole pipeline has landed (`cmake/modules/KonativeEmbedKotlinDex.cmake`) — verified via both a
direct `cmake --build` and a real `./gradlew assembleDebug`, both rendering correctly on-device; it
surfaced one more reflection-stripped-by-R8 class (`androidx.compose.ui.platform.
LifecycleRetainedValuesStoreOwner`, fixed in `r8-rules.pro`, same category as item 5 below) that
the hand-run recipe's own classpath snapshot had never hit.

## Adding to this folder

New `@Composable` UI, new `ActivityLifecycleCallbacks` behavior, new state — all real Kotlin here,
following ordinary Kotlin/Compose idioms (this folder is not bound by
`feedback-konative-coding-style`'s C++-specific `.hpp`-first/EnTT-metaprogramming rules, which apply
to the C++ core in `include/konative/**` and `src/**`, not to this JVM Kotlin module). Keep
`ComposeHostOwner` and `KonativeEntryPoint` together in one file, matching
`research/jni_activity_bootstrap_research.md` section 5.2's validated reference design, unless a
real second consumer of `ComposeHostOwner` justifies splitting it out. If a new dependency's own
classes get shrunk away by R8 despite being needed at runtime (reflection-only usage, exactly like
`KonativeEntryPoint`/`KonativeResourceProvider`/`kotlinx.coroutines.android.**`), add a targeted
`-keep` to `r8-rules.pro` with a comment citing the real crash/evidence - don't guess preemptively.
