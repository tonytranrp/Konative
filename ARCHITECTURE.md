# Konative — Architecture

> **STATUS (2026-07-18, updated): the full Compose/dex design in §6 is landed and verified on real
> hardware end to end, including the build automation — the `NativeActivity`/raw-EGL design this
> banner used to warn about is gone, not just flagged.**
> Earlier in this project's history, this document described Kotlin/Native + raw EGL/GLES rendering
> (no JVM, no dex). That was reversed: Konative's rendering is **JVM-hosted Jetpack Compose**, which
> fundamentally requires real JVM/ART (Compose cannot run on Kotlin/Native) — so dex embedding is
> back for the rendering/UI layer, built as one clean, self-checking CMake+C++ framework (in the
> spirit of corrosion) rather than GameHub's own ad-hoc per-module reflection code. §6 below is the
> current design, grounded in `research/incbin_embedding_research.md` and
> `research/jni_activity_bootstrap_research.md`. Every piece of it is now landed and verified on real
> hardware: `cmake/modules/KonativeEmbedBlob.cmake`'s `.incbin` embedder and
> `include/konative/embed/checked_blob.hpp`'s SHA-256 self-check (§6.5), the `JNI_OnLoad` entry point
> and dex-loader (§6.4), and the `kotlinc`+Compose-compiler-plugin+**`r8`** CMake pipeline
> (`KonativeEmbedKotlinDex.cmake`, §6.6) — real Jetpack Compose UI renders on-device, zero
> OpenGL/EGL/Vulkan anywhere. AAPT2 resource linking has since landed too (real `aapt2 compile`+`link`
> against the real dependency AARs, replacing the old hand-shimmed `embedded_kotlin/r_shim/`
> stopgap entirely — §6.6). The R8 `fill$default` optimizer bug is now root-caused and understood
> (§6.6, `embedded_kotlin/r8-rules.pro`) — `-dontoptimize` remains the fix, confirmed the only one
> that works on this toolchain after an exhaustive, empirical search for a narrower alternative. The
> runtime `resources.arsc` gap (real `Resources.getString()`-backed fields resolving correctly, not
> just having correct integer IDs) is now landed too, on BOTH sides: a general, comprehensive
> mechanism (`android.content.res.loader.ResourcesLoader`, API 30+, a second embedded resources.arsc
> blob) with a small, self-checking scoped patch as the automatic fallback below that API floor —
> neither needed touching `testapp/`'s own scope after all, so that decision never had to be forced.
> §6.7's status table states exactly what's landed (nearly everything, as of 2026-07-21). **A
> terminology note worth being honest about**: every "verified on real hardware"
> claim above, up through the R8 investigation, was verified on the rooted LDPlayer x86_64 *emulator*
> — "real hardware" meant "a real running Android OS instance reachable via `adb`," not literally
> non-emulated silicon. That distinction stopped being hypothetical on 2026-07-18: the full current
> pipeline was verified, for the first time, on an actual physical device (`R3GL10AHL7P`,
> Galaxy-S24-class, `arm64-v8a`) via a real `./gradlew assembleDebug` run producing one universal APK
> — see §13 and `testapp/README.md`. See the `project-konative-autonomous-loop` memory entry for the
> full iteration-by-iteration history.

This document is the synthesized design for Konative: a CMake/C++ framework combining Kotlin and
C++ into **one native Android `.so`** — rendering and app logic together. Everything below is
grounded in the research passes summarized in §15 (plus the rendering-direction reversal noted
above).

---

## 1. The decision: native fusion, not dex embedding (SUPERSEDED for rendering — see banner above)

The initial research pass (`research/research.md`) covered embedding compiled Kotlin/Java
*bytecode* (`classes.dex`) inside the `.so` and loading it at runtime via
`dalvik.system.InMemoryDexClassLoader` — the technique every major Android app-protection vendor
already uses. That path is **not** what Konative builds. Instead:

- Kotlin is compiled **ahead-of-time to real native machine code** via the **Kotlin/Native**
  compiler (`kotlinc-native` — a different compiler distribution from JVM-targeting `kotlinc`),
  producing a static library (`-produce static`) plus a generated C-ABI header.
  linked directly into the same `.so` as the C++ core — ordinary object code, not bytecode.
- There is **no JVM, no ART bytecode interpreter, no dex, and no JNI reflection bootstrap** at
  runtime. The Java/Kotlin side of the actual APK is reduced to a trivial bootstrap shim (see §6).
- Compile-time resolution — C++ templates, concepts, and EnTT-driven reflection/metaprogramming —
  does the work that dex-embedding would otherwise have needed JNI + ART runtime reflection for.
  This is "reflection to the max," but resolved at **compile time**, not via a classloader.

This mirrors what Konative's own `README.md` already stated ("Kotlin straight to a single native
`.so` via Kotlin/Native ... no JVM, no dex, no hand-written JNI ... the way `android-activity` does
for Rust") and what `GameHub`'s *other* CMake module — `cmake/modules/KotlinNative.cmake`, not
`JvmDex.cmake` — already does for its GLES-facing Kotlin logic.

---

## 2. Coding style (binding, not optional — see memory `feedback-konative-coding-style`)

- **`.hpp`-first.** Declarations *and* definitions live in headers by default — templates require
  this anyway, and the project leans into it everywhere. `.cpp` is reserved for `main.cpp` and
  other genuinely load-bearing translation units (explicit template instantiation choke points,
  the Kotlin/Native C-ABI bridge implementation, the Android entry point) — never a default.
- **Heavy compile-time metaprogramming.** CRTP for static polymorphism, C++20 concepts in place of
  SFINAE, variadic templates + `if constexpr`, template-template parameters where a policy-based
  configuration layer earns its keep. Runtime type-erasure (`std::variant`+`overloaded` for closed
  event-type sets; Sean-Parent-style value-semantic type erasure for open-ended, non-intrusive
  polymorphism) is used deliberately, not as a default escape hatch.
- **EnTT is the reflection/ECS/event backbone**, fetched via CPM (§4). Konative sits on the
  "pure-template, zero-codegen" pole of the reflection landscape (alongside RTTR, Boost.PFR,
  Glaze) rather than Unreal/Refureku's codegen-tool pole — no separate code-generation pre-build
  step, ever.
- **One `.hpp` per event type**, all under `include/konative/events/`, organized into
  feature-area subfolders (`lifecycle/`, `window/`, `input/`, ...). A single shared
  `events/dispatcher.hpp` holds the generic dispatch machinery — the dominant real-world pattern
  (Hazel, EnTT itself, eventpp, tinyevents) is **one generic dispatcher, many small event types**,
  never a dispatcher per module. This is a deliberate divergence from both `GameHub`'s own
  `libs/events/` (which batches all event types into one shared header) and from Hazel's `events/`
  folder (which batches by *category*, not truly one-per-type, despite superficially looking that
  way) — Konative's rule is stricter than either precedent it's inspired by.
- **Deep, nested folder trees.** Every mature header-only C++ library surveyed (EnTT, GLM, Eigen,
  Boost.Hana, range-v3) nests at least 2 levels (`<lib>/<module>/<file>.hpp`), and the
  larger/older ones go 3 once a module's internals justify it. Konative follows the same rule:
  split by feature/module area, not by "everything in one flat `src/`."
- **`detail/` subfolder + `namespace detail`** inside every module, mirroring Boost/GLM/Hana's
  near-universal convention — implementation-only entities that can't be hidden behind a `.cpp`
  boundary (because they're templates) get a documented, "don't use this" wall instead.
- **`#pragma once`**, not include guards — avoids macro-name collisions across a tree that will
  have hundreds of small headers (the event-per-file convention alone guarantees this).
- **CMake `FILE_SET HEADERS`** (CMake ≥3.23) for declaring/installing headers per module, combined
  with one `CMakeLists.txt` per subfolder (`add_subdirectory()` chains) — never
  `file(GLOB_RECURSE ...)`, which CMake's own docs warn against and which is a bad fit for a
  folder (`events/`) that gains new files constantly.
- **Compile-time cost is a first-class design constraint, not an afterthought.** Real horror
  stories exist for exactly this kind of codebase (range-v3's 6x build-time regression, EnTT's own
  compile-time-conscious module-splitting advice). Mitigations budgeted in from day one: a shared
  precompiled-header target for stable third-party deps (never Konative's own frequently-edited
  headers), explicit template instantiation for the small number of genuinely closed type sets,
  and forward-declaration (`fwd.hpp`-style) headers to avoid dragging heavy template definitions
  into translation units that only need a name.

---

## 3. Reflection & ECS layer — EnTT

EnTT (`skypjack/entt`) supplies three things Konative uses directly:

1. **`entt::meta`** — non-intrusive, macro-free runtime reflection built entirely from compile-time
   template machinery (NTTP deduction over pointer-to-member/function, SFINAE-based traits,
   `constexpr` FNV-1a hashing via `entt::hashed_string`). This is the "reflection to the max via
   templates" layer, and it's genuinely compile-time metaprogramming producing an *optional*
   runtime query surface — not the other way around.
2. **`entt::registry`** — the ECS core (sparse-set storage, views, groups, reactive on_construct/
   on_update hooks) for any data-driven app/scene state Konative wants to model as entities +
   components, e.g. render-visible nodes, input targets, or Kotlin/Native-backed logic objects.
3. **`entt::dispatcher`** (+ `entt::sigh`/`entt::delegate`) — the event bus. `trigger<E>()` for
   immediate synchronous dispatch, `enqueue<E>()`/`update()` for deferred, once-per-frame-flush
   delivery. Konative's per-event-type `.hpp` convention (§2) supplies the `E` types; EnTT supplies
   the generic bus that's agnostic to what `E` actually is.

**Known real gaps to design around, not ignore:**
- `entt::registry` has no built-in way to construct a component pool from only a `type_info`/hash —
  generic "add component by reflected type" tooling needs an explicit, per-type-registered
  emplace/factory thunk (`entt::meta`'s `.func<>()` closing over the concrete C++ type), confirmed
  directly by EnTT's own maintainer. Konative's editor/serialization layer must register this
  thunk alongside every `entt::meta`-reflected component type, not assume the registry does it for
  free.
- Neither `entt::registry` nor `entt::dispatcher` is internally thread-safe — see §5.
- No EnTT-maintainer-published guidance exists on `entt::meta` compile-time cost at scale; the
  mitigations in §2 are general C++ template-heavy-project practice applied to EnTT's specific
  registration API, not EnTT doctrine.
- Combining `entt::meta` with Boost.PFR (to auto-generate registration calls from aggregate
  reflection instead of hand-writing `.data<>()` per field) and combining `entt::meta` with Glaze
  (§4) are both **architecturally sound but unvalidated in the wild** — no third-party project
  doing either was found. Treat both as prototype-first, not copy-from-precedent.
- **C++20 minimum.** Current EnTT (`main`) requires "a full-featured compiler that supports at
  least C++20" — stricter than older EnTT tags. Pin to a specific tag matching the NDK Clang
  version actually in use, and verify C++20 conformance before tracking `main`.

---

## 4. Dependencies — fetched via CPM.cmake, vendored in-tree

`cmake/CPM.cmake` is committed directly to the repo (the CPM project's own documented
offline-reproducibility pattern — CPM's own wiki confirms the alternative "download fresh at
configure time" pattern **cannot** deliver offline reproducibility even with a warm source cache,
so vendoring is the only option consistent with `GameHub`'s existing offline-build precedent in
the same workspace). **Every `GIT_TAG` must be an immutable tag or commit hash, never a branch
name** — CPM's own issue tracker confirms offline mode silently breaks against a moving branch ref
even with `CPM_SOURCE_CACHE` populated.

| Library | Role | Header-only | CPM shape |
|---|---|---|---|
| [EnTT](https://github.com/skypjack/entt) | Reflection, ECS, events | Yes | `DOWNLOAD_ONLY` + hand-rolled `INTERFACE` target (EnTT's own `CMakeLists.txt` is for its test/doc suite, not for consumption — CPM's own official example does exactly this) |
| [GLM](https://github.com/g-truc/glm) | Math for **ECS-side** transforms/components (not rendering — §6.2 moved all rendering math into Kotlin/Native, so GLM's C++ role is now narrower than earlier drafts assumed) | Yes | Standard `CPMAddPackage`, `glm::glm` target, linked wherever a C++ system/component actually needs vector/matrix math. Default to GLM's **packed** (non-SIMD-aligned) types for EnTT component storage until the aligned-type + EnTT-paged-storage combination is verified in CI |
| [spdlog](https://github.com/gabime/spdlog) + [fmt](https://github.com/fmtlib/fmt) | Logging | Header-only mode available (`SPDLOG_FMT_EXTERNAL_HO`) | spdlog ships a first-class Android logcat sink — auto-links the NDK `log` library |
| [Taskflow](https://github.com/taskflow/taskflow) | DAG-based job scheduling for ECS systems | Yes | Default scheduler for anything with real cross-system dependencies |
| [BS::thread_pool](https://github.com/bshoshany/thread-pool) | Simple thread pool | Yes | Use for subsystems that don't need a task graph — don't run both schedulers side by side without a reason |
| [concurrentqueue](https://github.com/cameron314/concurrentqueue) + [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) | Cross-thread event/job posting | Yes | The producer/consumer boundary in front of `entt::dispatcher` (§5) |
| [libcoro](https://github.com/jbaldwin/libcoro) | C++20 coroutines | No (compiled) | The only concurrency library surveyed with **explicit documented Android NDK per-ABI support** — strongest maintenance signal found for this exact target |
| [Glaze](https://github.com/stephenberry/glaze) | Config/hot-reload JSON | Yes | Macro-free compile-time reflection — same non-intrusive philosophy as `entt::meta`; unvalidated combination, prototype first |
| [cereal](https://github.com/USCiLab/cereal) | Binary save-state snapshots | Yes | EnTT's own historically-documented snapshot-API pairing |
| [Catch2](https://github.com/catchorg/Catch2) or [doctest](https://github.com/doctest/doctest) | Testing | Yes (v2/doctest) | doctest specifically marketed for near-zero compile-time overhead — relevant given §2's compile-time budget |

**Explicitly rejected, with reasons:**
- **oneTBB** — not header-only, documented Android NDK linker breakage on newer NDK versions
  (open upstream issue), built for HPC/server workloads Konative doesn't have.
- **marl** — archived by Google (2026-04-27), read-only; reviving/forking an archived scheduler is
  exactly the "hand-rolled infrastructure maintenance" trap this project is avoiding.
- **junction** (concurrent hash map) — needs a separate companion dependency (Turf), weaker
  release signal than libcuckoo, and the actual use case (event posting) doesn't need a concurrent
  map at all — a queue solves it more directly.
- **A dedicated DI framework** (Boost.DI / kangaru) — deliberately **not adopted**. EnTT's own
  `entt::registry::ctx()` (the per-registry context/service store) already *is* Konative's
  composition root for cross-cutting services; layering a second, competing compile-time DI system
  on top adds contributor cognitive load and compile-time cost for a problem already solved.

---

## 5. Concurrency model

- `entt::registry` and `entt::dispatcher` are **not internally thread-safe** (confirmed directly
  against EnTT's own issue tracker). The sanctioned pattern for parallel ECS work, per EnTT's own
  maintainer: get a view's splittable index range via `view.handle()`, partition it across tasks,
  each task does `.get<Components...>()` by index — EnTT never spawns anything, never owns a
  thread; the job system (Taskflow/BS::thread_pool) owns all of that.
- Cross-thread **event posting** into the single-threaded `entt::dispatcher` goes through a
  `concurrentqueue` MPMC queue: worker threads `enqueue()` onto the lock-free queue; exactly one
  thread (the main/frame thread) drains it into `dispatcher.enqueue<E>()` before calling
  `dispatcher.update()` once per frame. This keeps `entt::dispatcher` single-threaded-safe by
  construction, not by discipline.
- Coroutines (libcoro) are for async/event-driven logic that wants to `co_await` rather than poll
  — e.g., an "await the next `E`" pattern built from a one-shot dispatcher sink resolving a
  `coro::event`. This is an architecturally sound synthesis, not a validated pattern from prior
  art — prototype and test before relying on it broadly.

---

## 6. Android integration: `JNI_OnLoad`, embedded-dex Compose, and the self-checking loader

`NativeActivity`/`GameActivity`/`android_native_app_glue` — §6.1's subject below — **no longer
apply to the rendering/app target at all.** `NativeActivity` takes over the whole window as a raw
`Surface`; it cannot host a `setContentView(composeView)` tree at all, which rules it out
categorically once rendering is JVM-hosted Compose rather than raw EGL. `testapp/`'s manifest
already reflects this (a plain `Activity`, not `NativeActivity`) and
`src/platform/android/{android_main.cpp,activity_bridge.cpp,looper_pump.cpp}` were consequently dead
code as of this rewrite, then actually deleted alongside the `JNI_OnLoad` rewrite landing (§6.7) —
confirmed gone from the real tree, not merely scheduled. **§6.1–6.3 below are kept, not deleted, and
are the historical
design those dead files (plus `native/`, `include/konative/{interop,render}/`, and a number of
other still-existing files) were written against** — many real, currently-live files still cite
these exact section numbers for mechanics that remain individually true (the `@CName`/`_api.h`
boundary shape, the documented Kotlin/Native+NDK linking risk, the `Result<T,E>`-at-interop-
boundaries rule) even though none of it is the current rendering path. Do not implement new
rendering code against §6.1–6.3 — the current design is §6.4 onward.

### 6.1 [SUPERSEDED for rendering — kept for `native/`/`interop/`/`render/`'s still-existing code] Entry point and event loop

`android_native_app_glue` (or Google's newer **GameActivity**, the actively recommended
replacement) supplies the native event loop: an `android_app` struct, an `ALooper`-based command
pump, and lifecycle callbacks (`APP_CMD_INIT_WINDOW`/`APP_CMD_TERM_WINDOW`/`APP_CMD_GAINED_FOCUS`/
etc.) fired from a dedicated native thread. Two real options, with a real trade-off, matching the
same choice Rust's `android-activity` crate (Konative's own stated model) exposes as a feature
flag:

| | `NativeActivity` | `GameActivity` |
|---|---|---|
| Kotlin/Java footprint | **Zero** — `android:hasCode="false"` + manifest `android.app.lib_name` meta-data only | One trivial subclass required (`GameActivity` cannot be the launcher Activity itself); its whole body can be a `System.loadLibrary()` call in an `init {}` block |
| Rendering surface | Direct `ANativeWindow` ownership | Renders into a `SurfaceView`, giving access to other Android UI/AppCompat/Jetpack elements |
| Input | Raw `AInputQueue`/`ALooper` pump | `InputBuffer` swap-buffer API in the Java `GameActivity` class; adds soft-keyboard/IME support `NativeActivity` historically lacked |
| Wiring | Ships in the NDK itself | Gradle `androidx.games:games-activity` + Prefab; CMake `find_package(game-activity REQUIRED CONFIG)` |

Real-world precedent (Rust's `android-activity`) confirms **both are viable and the choice is a
real feature-flag-level decision, not a default to assume**: it recommends `NativeActivity` "if
you may not need to compile/link any Java or Kotlin code," and only requires the trivial subclass
under `GameActivity`. This choice is now moot for Konative's own rendering/app target (§6.4 replaces
it categorically), but the table above remains accurate documentation for the still-existing
`src/platform/android/*` code until it's actually deleted.

### 6.2 [SUPERSEDED for rendering — kept for the same reason] Rendering lives in Kotlin/Native, not in C++ — a deliberate revision

An earlier pass of this skeleton gave Konative its own hand-written C++ EGL/GLES/Vulkan backend
(`render/backend/gles/`, `render/backend/vulkan/`). **That backend has been deleted.** It
duplicated exactly the responsibility Kotlin/Native already lets Kotlin own directly. Splitting
rendering across *two* languages (a C++ backend calling EGL, plus Kotlin/Native logic calling back
into that C++ backend) added a second interop hop for no benefit — Konative's whole premise (at the
time) was that Kotlin/Native code is real native code, fully capable of calling
`eglGetDisplay`/`glClear`/`eglSwapBuffers` itself. **This premise itself is now superseded for
rendering specifically** (this document's top banner, and §6.4 onward) — Compose needs the real
JVM/ART object model Kotlin/Native categorically cannot provide, not just an EGL surface.

**A load-bearing finding, still true, still potentially useful for non-rendering native logic**:
Kotlin/Native ships pre-built cinterop bindings for EGL, GLES2/GLES3, and core Android NDK types
(`ANativeWindow`, `ANativeActivity`) for every `androidNative*` target **out of the box** —
`platform.egl`/`platform.gles2`/`platform.gles3`/`platform.android`, sourced from
`kotlin-native/platformLibs/src/platform/android/*.def` in the JetBrains/kotlin compiler
distribution itself. No custom `.def` file, no manual NDK sysroot `-I` wiring, is needed for GLES
access — `native/cinterop/` is correctly **empty** in this skeleton as a result; it's reserved for
a genuinely un-bundled C API (Vulkan is the one confirmed example — no `vulkan.def` ships).

There is also **real, working, published prior art** for this combination:
[**natario1/Egloo**](https://github.com/natario1/Egloo), a maintained Kotlin Multiplatform library
that binds EGL+GLES for `androidNative*` targets (published `.klib` artifacts for
`androidNativeArm64`/`X64`/`Arm32`/`X86`), with real `androidNativeMain` source doing exactly the
`platform.egl`/`platform.gles2` calls (`eglGetDisplay`, `eglInitialize`, `eglCreateWindowSurface`
from an `EGLNativeWindowType`, `eglMakeCurrent`, `eglSwapBuffers`, GLES draw calls) —
`native/src/Renderer.kt`'s own top comment points at Egloo's `egl.kt`/`EglCore.kt` as the reference
for the config/context/surface-creation sequence, should this code ever be revived for a
non-rendering purpose.

`native/src/Renderer.kt` (currently implemented only through `eglInitialize`, with the rest
deliberately left as a TODO) is frozen as a historical spike, not extended further — see §6.7's
status table. **No `EGL/`, `GLES*/`, or `vulkan/` header may ever be included anywhere under
`include/konative/`** remains a live, enforced hard rule regardless (`render/README.md`), since
`include/konative/` itself isn't specific to any one rendering approach.

### 6.3 [SUPERSEDED for rendering — kept for the same reason, and because its core principle is still current] The Kotlin/Native ⇄ C++ boundary — and its real, documented risk

Kotlin/Native's `-produce static` mode emits a static archive plus a generated `_api.h` C-ABI
header (nested function-pointer-table struct, manual `DisposeString`/`DisposeStablePointer`
lifetime management). The far more usable boundary primitive for hand-written interop is
**`@CName("flat_symbol_name")`** on individual top-level Kotlin functions — this exports an
ordinary flat C symbol resolvable via a plain `extern "C"` declaration on the C++ side, avoiding
the nested-struct API entirely. The reverse direction (Kotlin/Native calling hand-written C++)
goes through `cinterop` consuming a plain C header exposed by the C++ core — the same general
`cinterop` mechanism that (per §6.2) already ships pre-built for EGL/GLES/`ANativeWindow` and would
need a hand-authored `.def` file only for something genuinely un-bundled like Vulkan — with no JNI
anywhere in either direction, for any of it.

**Flag this risk explicitly, do not paper over it**: this exact combination (Kotlin/Native static
lib linked into an Android NDK CMake C++ target) has **real, documented, unresolved community
friction** — a JetBrains issue (kotlin-native#2803, archived 2021 with no recorded fix) reports
unresolved `libc++`/libm symbols when linking a Kotlin/Native `android_arm32` static lib into an
NDK CMake project, and a separate Kotlin Discussions thread reports an `UnsatisfiedLinkError` for
a hand-written C function called from a cinterop-consumed static lib on Android. The Android
Kotlin/Native targets (`androidNativeArm32/64`, `androidNativeX86/X64`) are also officially
**Tier 3** — "not guaranteed to be tested on CI... use with caution." This remains real,
documented risk for `native/`'s existing code (should it ever be revived for a non-rendering
purpose) — it just no longer blocks anything on Konative's current critical path, since that path
no longer depends on this specific link succeeding (§9).

**The one part of this subsection that outlives the Kotlin/Native context entirely**: prefer
`Result<T, E>` (`core/result.hpp`) over an exception at *any* interop boundary a C++ exception
cannot safely unwind across — true of the old Kotlin/Native `@CName` boundary, equally true of the
new `JNI_OnLoad`/JNI boundary (§6.4/§6.5). `core/README.md` and `embed/README.md` both cite this
same principle for exactly that reason.

### 6.4 Entry point: `JNI_OnLoad`, not `android_main`

`testapp/`'s one-and-only `.kt` file is, in full:

```kotlin
class MainActivity : Activity() {
    companion object { init { System.loadLibrary("konative_app_native") } }
}
```

It never overrides `onCreate()` or calls `setContentView()` — every bit of that behavior must
originate from native code, triggered only by the `JNI_OnLoad(JavaVM*, void*)` callback
`System.loadLibrary()` invokes automatically. This is guaranteed to run to completion **before**
`MainActivity.onCreate()` can execute, independent of any Android-version-specific trivia: Kotlin
compiles the companion object's `init {}` block into `MainActivity`'s `<clinit>`, and the JLS/JVM
spec guarantees a class's static initializer completes before any instance of that class can be
constructed — `onCreate()` cannot run on an instance that doesn't exist yet.

Full design, all JNI signatures, and the hidden-API research behind step 1 below:
`research/jni_activity_bootstrap_research.md` §5.

1. **The one hidden-API call in this whole design**: `ActivityThread.currentApplication()` via
   ordinary JNI reflection (`FindClass`/`GetStaticMethodID`/`CallStaticObjectMethod`) — verified
   directly against Google's own published `hiddenapi-flags.csv` to be `unsupported`
   (usable-indefinitely tier, never `max-target-X`/`blocked`) continuously from API 29 through the
   current release, which covers this project's own API 26–36 target range.
2. **Verify, then load, the embedded dex** — §6.5/§6.6 below.
3. **One handoff call, then native code is done**: `CallStaticVoidMethod` invoking a single
   `@JvmStatic fun install(application: Application)` entry point in the freshly-loaded dex class.
   Everything past this point — registering `Application.ActivityLifecycleCallbacks`, fabricating
   the `LifecycleOwner`/`ViewModelStoreOwner`/`SavedStateRegistryOwner` a plain `Activity` doesn't
   provide automatically, building the `ComposeView`, calling `activity.setContentView(view)` — is
   real, compiled Kotlin running as real loaded JVM bytecode, not further JNI reflection. Compose's
   own APIs change far more often than `ActivityLifecycleCallbacks`; keeping them in Kotlin means
   Konative's own build catches breakage at compile time instead of a user's device at runtime.

### 6.5 The embedded blob: build-time, `.incbin`, self-checking — landed and verified

**Shipped and verified on real hardware** (both arm64-v8a and x86_64, via the real installed NDK
r28 — see the `project-konative-autonomous-loop` memory log, iterations 7–9 for the full
verification history including one real bug caught and fixed):

- `cmake/modules/KonativeEmbedBlob.cmake`'s `konative_embed_binary_blob(<target> BLOB <path>
  SYMBOL <prefix> [VERIFY_SHA256])` embeds an arbitrary file as linked read-only data via a GAS
  `.incbin` directive (chosen over C-array text generation, which chokes lexing a multi-MB
  generated source file — `research/incbin_embedding_research.md` §§1–2, 4), exposing
  `extern "C" { extern const unsigned char <prefix>_start[]; extern const unsigned char
  <prefix>_end[]; extern const uint64_t <prefix>_size; }`. Requires the including project to have
  already called `enable_language(ASM)` (Konative's own top-level `CMakeLists.txt` does this inside
  its `if(ANDROID)` block) — the function itself deliberately does not call it, after a real,
  reproduced bug proved doing so only works as a *redundant* second call, never as the first.
- `VERIFY_SHA256` additionally computes the blob's real SHA-256 at build time (`file(SHA256 ...)`,
  CMake's own builtin) and embeds it as `<prefix>_expected_sha256[32]`.
- `include/konative/embed/checked_blob.hpp`'s `verify_blob()` is the runtime counterpart: re-hashes
  the actual embedded bytes (via `PicoSHA2`) and compares against that constant, returning
  `Result<span, BlobVerifyError>` rather than throwing (the JNI boundary is exactly the kind of
  interop boundary §6.3 used to warn a C++ exception can't safely cross — that constraint carries
  over unchanged from the old Kotlin/Native design to this one). **This is a build-integrity
  self-check, catching "the build pipeline embedded the wrong/truncated bytes," not a tamper/
  security boundary** — the expected hash sits in cleartext right next to the data it verifies.

### 6.6 The embedded dex: landed, and rendering for real on real hardware

**Landed and proven, not just compiled**: the real Kotlin source (`embedded_kotlin/`) —
`KonativeEntryPoint`/`ComposeHostOwner`/a trivial `@Composable` proof UI, implementing
`research/jni_activity_bootstrap_research.md` §5.2's design directly — **actually renders on real
hardware**: a screenshotted green `Box` with white "Konative" text, driven entirely by
`JNI_OnLoad` → `verify_blob()` → `load_class_from_dex()` → `install(Application, ByteBuffer?)` →
`ActivityLifecycleCallbacks` → `ComposeView` → real Jetpack Compose composition, no OpenGL/EGL/
Vulkan anywhere. See `embedded_kotlin/README.md`'s Status section for the full, real bug list this
took (five distinct real issues, each with its own root cause and fix, not just "it compiled").

Compiles cleanly and R8-shrinks to a single ~1.2–2.4MB `classes.dex` against the real,
Gradle-resolved AndroidX dependency closure, using `embedded_kotlin/r8-rules.pro` (now a real,
committed file, not scratchpad-only) — this answers the size question below with a real, measured
number.

**The `kotlinc`+Compose-compiler-plugin+`r8` CMake pipeline is now landed**:
`cmake/modules/KonativeEmbedKotlinDex.cmake`'s `konative_embed_kotlin_dex(<target> SOURCES <kt-
files> PG_CONF <path> SYMBOL <prefix>)` automates the full hand-validated recipe at build time
(`KonativeCompileKotlinDex.cmake`, a `cmake -P` driver following the same shape as
`KonativeGenerateIncbinAsm.cmake`), then hands the resulting `classes.dex` to the existing
`konative_embed_binary_blob()`. `src/platform/android/CMakeLists.txt` uses this automatically
unless `KONATIVE_EMBEDDED_DEX_PATH` is set (a manual-override escape hatch, still supported — that
branch also embeds a real resources.arsc if `KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH` is set, or a
real empty placeholder otherwise, so it keeps linking now that the automated path embeds a second,
sibling blob too — see `testapp/README.md`).
Requires three machine-local toolchain paths (`KONATIVE_KOTLINC`, `KONATIVE_R8`,
`KONATIVE_ANDROID_JAR`) plus `KONATIVE_KOTLIN_CLASSPATH_DIR` (a pre-resolved dependency-jar
directory — real Maven resolution from CMake is still not solved, see below), set the same way
`ANDROID_NDK_HOME` already is (`CMakeUserPresets.json`, machine-local, gitignored).
**Verified end-to-end, not just "the script exits 0"**: a real `cmake --build`, and separately a
real `./gradlew assembleDebug` (via `testapp/`'s own `externalNativeBuild`, with the new toolchain
paths forwarded as Gradle properties), both produced a working `.so`, installed and rendered
correctly on the rooted LDPlayer x86_64 emulator — the identical green-Box-plus-"Konative"-text
output as the original hand-built milestone.
**Automating the pipeline surfaced one genuinely new reflection-stripped-by-R8 bug the hand-run
recipe's specific classpath snapshot never happened to hit**: `androidx.compose.ui.platform.
LifecycleRetainedValuesStoreOwner` (constructed via `ViewModelProvider.NewInstanceFactory`
reflection, same "invisible to R8's shrinker" shape as `KonativeEntryPoint`/
`KonativeResourceProvider`/`kotlinx.coroutines.android.**`) — fixed with one more `-keep` rule in
`embedded_kotlin/r8-rules.pro`, confirmed via `javap` against the real dependency jar that the
class does declare the no-arg constructor R8 was stripping.

- **The "one `.kt` file in `testapp/`" rule does not reach the embedded dex.** Two independent
  build pipelines exist: `testapp/`'s own Gradle/AGP pass compiles exactly `MainActivity.kt`;
  Konative's own CMake-driven pipeline compiles `embedded_kotlin/`'s separate Kotlin source tree
  into the dex blob that gets embedded as opaque bytes in the `.so` — `testapp/`'s build never
  sees those `.kt` files at all.
- **Embedded-blob size, previously flagged as unmeasured, is now real and measured**: a naively
  `d8`-dexed full Compose+lifecycle+savedstate+coroutines runtime (no Material3 — see
  `embedded_kotlin/README.md`'s hard rule on why) is **~20MB and splits into 2 dex files**
  (Konative's current `load_class_from_dex()` only accepts one dex buffer). R8-shrunk (dead-code
  removal only — obfuscation and even some optimization passes had to be disabled, see
  `embedded_kotlin/r8-rules.pro`), it fits in a **single ~1.2–2.4MB dex** comfortably.
- **The `Dispatchers.Main` blocker — solved, two distinct real causes, both required**:
  `ComposeView` attachment unconditionally constructs a `FontFamilyResolver`, which touches
  `kotlinx.coroutines.Dispatchers.Main`. First cause: `dalvik.system.InMemoryDexClassLoader` loads
  bytecode from a raw byte buffer with no JAR/ZIP resource backing, so
  `ClassLoader.getResource()`/`getResourceAsStream()` always return nothing for a dex-loaded
  class. Fixed with `KonativeResourceProvider` (a plain `ClassLoader` — NOT an
  `InMemoryDexClassLoader` subclass, that class is `final`, verified by a real compile error) used
  as a second `InMemoryDexClassLoader`'s parent, relying on `ClassLoader.getResource()`'s standard
  parent-first delegation; `jni_onload.cpp`'s `load_class_from_dex()` bootstraps this
  opportunistically via `dex_loader.hpp`'s `detail::upgrade_to_resource_aware_loader()`. Second,
  independent cause (an earlier version of this document claimed only the first was needed — real,
  on-device testing proved that claim incomplete): `kotlinx-coroutines`'s Android-specific "fast"
  dispatcher-discovery path (`FastServiceLoader`, confirmed by decompiling it directly) doesn't
  need resources at all on a real device — it uses a hardcoded `Class.forName(...)` lookup — but
  R8 had silently shrunk away both `kotlinx.coroutines.android.**` and `KonativeResourceProvider`
  itself, since neither is referenced by anything in the static call graph (both are only reached
  via reflection). Both needed explicit `-keep` rules (`embedded_kotlin/r8-rules.pro`) — confirmed
  by testing that the resource-provider fix alone did not solve the crash, and the keep rules alone
  also did not; the combination, tested last, did.
- Real, working reflection precedent for the dex-loading mechanics themselves (steps between §6.4's
  step 1 and step 3): `GameHub/libs/jni/src/dex_loader.cpp`'s already-proven
  `getClassLoader()`→`NewDirectByteBuffer`→`InMemoryDexClassLoader`→`loadClass()`-via-reflection
  sequence, already ported into `include/konative/jni/dex_loader.hpp` (§6.7).
- **A second, real gap the hand-rolled pipeline surfaced — since fixed, with one deeper gap found
  and documented, not fixed, along the way (2026-07-18)**: this pipeline originally had no AAPT2
  resource-linking step, so several AndroidX libraries' own `R$id`/`R$string` classes (real
  resource-ID constants those libraries need, e.g. for `View.setTag()`-based owner attachment)
  didn't exist — worked around at the time with hand-written stand-ins (`embedded_kotlin/r_shim/`).
  **A real AAPT2-based fix has since landed** (`KonativeCompileKotlinDex.cmake`'s Step 1.5: `aapt2
  compile`+`link` against the real dependency AARs, `javac`-compiled straight into the classes R8
  already dexes) — `r_shim/` is deleted, fully replaced, verified via a real build plus a fresh
  on-device deploy with no regression. This fully solves the build-time/classload-time correctness
  problem `r_shim/` existed for. **It does NOT solve a separate, deeper problem found while building
  it**: fields backed by `Resources.getString()`/a real `resources.arsc` table (`R$string`/`R$style`/
  `R$styleable`) still won't resolve at true runtime, since neither this module nor `testapp/`'s own
  APK packages any `res/` content — confirmed via `javap` that a class already in the current shipped
  dex (`AndroidComposeViewAccessibilityDelegateCompat`) makes exactly this kind of call on exactly
  the two fields `r_shim/` used to shim, a live not-yet-triggered bug, not a hypothetical. See
  `embedded_kotlin/README.md`'s 2026-07-18 update for the full writeup and why the real fix needs a
  deliberate decision about `testapp/`'s own resource-packaging scope, not something to redefine
  silently as a side effect of this fix.

### 6.7 Status: what's landed vs. still open, as of this rewrite

| Piece | Status |
|---|---|
| `.incbin` blob embedder + build-time SHA-256 (`KonativeEmbedBlob.cmake`) | **Landed, verified on real arm64-v8a + x86_64 hardware** |
| Runtime SHA-256 self-check (`include/konative/embed/checked_blob.hpp`) | **Landed, verified (desktop unit tests + real on-device round-trip)** |
| Dex-loader (`InMemoryDexClassLoader` construction, `include/konative/jni/`) | **Landed, ported from `GameHub`** |
| `JNI_OnLoad` entry point (`src/platform/android/jni_onload.cpp`) | **Landed** |
| **Full `JNI_OnLoad` → `verify_blob()` → `load_class_from_dex()` → reflective `install(Application, ByteBuffer?)` chain** | **Verified end-to-end on real hardware** with both a placeholder entry point (real logcat proof) and the real Compose entry point (real screenshotted rendering) |
| `KonativeResourceProvider` (`embedded_kotlin/`) — resource lookup for a dex-loaded `ClassLoader` | **Landed, verified on real hardware** — see §6.6 |
| `kotlinc`+Compose-compiler-plugin+`r8` CMake pipeline (`KonativeEmbedKotlinDex.cmake`) | **Landed and verified** — real `cmake --build` and real `./gradlew assembleDebug`, both rendering correctly on-device (§6.6) |
| Embedded Kotlin+Compose source tree (`embedded_kotlin/KonativeEntryPoint`) | **Landed and rendering on real hardware** — real screenshot proof (§6.6), no known blockers remaining for this proof-of-concept's scope |
| AAPT2 resource linking for the embedded dex (real `R$id`/`R$string`/etc. values) | **Landed and verified** — `KonativeCompileKotlinDex.cmake` Step 1.5, replaces the deleted `embedded_kotlin/r_shim/` stopgap entirely (§6.6) |
| Runtime `resources.arsc` backing for `Resources.getString()`-class fields (`R$string`/`R$style`/`R$styleable`) | **Landed and verified on real hardware, both mechanisms** — the general, comprehensive fix (`android.content.res.loader.ResourcesLoader`, API 30+, `embedded_kotlin/KonativeResourcesLoader.kt`, a second embedded `resources.arsc` blob) with `embedded_kotlin/KonativeResourceStringOverride.kt`'s smaller, scoped patch as the automatic fallback below that API floor; see `embedded_kotlin/README.md`'s Update sections for the full writeup of both |
| `src/platform/android/{android_main,activity_bridge,looper_pump}.cpp` + `include/konative/platform/android/` | **Removed** (superseded by `jni_onload.cpp` + `include/konative/jni/`) |
| `testapp/`'s Gradle build (`app/build.gradle.kts`) | **Landed** — drives the real root `CMakeLists.txt` for `konative_app_native` end to end (CPM fetch, Android NDK cross-compile, automated Kotlin+Compose dex build, `.incbin` embed, dex packaging); `-PkonativeEmbeddedDexPath=<path>` (see `testapp/README.md`) remains available as a manual override, no longer required |
| Kotlin/Native (`native/src/Renderer.kt`, EGL/GLES rendering) | **Fully superseded for rendering** — kept only as a historical record; do not extend |
| Cross-thread event posting (`include/konative/scheduling/cross_thread_event_queue.hpp`) | **Landed and verified** — `konative::scheduling::CrossThreadEventQueue<Event>`, the `concurrentqueue`-backed MPMC boundary `events/dispatcher.hpp` and this module's own README had long specified but never implemented; `post()` from any thread, `drain_into(Dispatcher&)` from exactly one (the frame thread). Desktop-verified with a real multi-`std::thread` stress test (8 producers × 5000 events), confirming none lost, none duplicated |
| C++ ECS/events core (`World`/`Application`) actually running in the real app, driven by real Android Activity lifecycle AND a real per-frame heartbeat | **Landed and verified on real hardware** — previously buildable since the first commit but never instantiated anywhere outside desktop tests/examples (the "real open item, not yet decided" `include/konative/app/entry_point.hpp` and `detail/lifecycle_bridge.hpp` both flagged). `jni_onload.cpp` implements `create_application()` and binds `KonativeEntryPoint.nativeDispatchLifecycle(Int)`/`nativeTick(Float)` via `RegisterNatives`; the embedded dex's existing `ActivityLifecycleCallbacks` calls the former on each of the 4 transitions `Application` models, and a `Choreographer.FrameCallback` (`FrameTicker`, started/stopped alongside resume/pause) calls the latter once per real display frame, driving `World::tick()` for the first time — confirmed via real logcat across a full start→resume→pause→resume→destroy session with periodic tick-count proof, plus new desktop unit coverage (`tests/test_app.cpp`) |
| Taskflow real-hardware self-check (`include/konative/scheduling/taskflow_self_check.hpp`) | **Landed and verified on real hardware** — resolves §9's own "no confirmed track record either way" flag for Taskflow on Android NDK; runs a real, verifiably-correct parallel computation once at real startup (`jni_onload.cpp`'s `on_started()`), confirmed PASSED on the physical phone (arm64-v8a, API 36) via logcat, kept as a permanent regression guard rather than a one-off spike |
| Real C++-side state visible in the rendered Compose UI (`KonativeRootComposable`'s "C++ ticks: N" line) | **Landed and verified on real hardware, visually** — a third `RegisterNatives` binding, `nativeGetTickCount()`, lets Kotlin query `KonativeAndroidApp::tick_count()` every real frame and feed it into a `mutableStateOf` a composable reads, closing the loop from "the C++ ECS ticks, but only logcat ever sees it" to something actually on screen. Confirmed via two real screenshots ~5s apart on the physical phone showing the displayed count genuinely increasing (871 → 2288), not just present |

---

## 7. Build system: CMake wrapping Kotlin/Native the way corrosion wraps Cargo

The `research/research.md` corrosion deep-dive (§3 there) still applies architecturally, just
retargeted from "wrap `cargo rustc`" to "wrap `kotlinc-native`":

- **Delegate to the real toolchain binary** (`kotlinc-native -produce static -target
  android_arm64 ...`) via `add_custom_command`/`add_custom_target` — never reimplement Kotlin/Native
  compilation semantics in CMake. `GameHub/cmake/modules/KotlinNative.cmake` is the working
  reference implementation of exactly this for the same compiler.
- **Predict artifact paths deterministically** rather than globbing the output directory after
  the fact — Kotlin/Native's output naming (`lib<name>.a` + non-lib-prefixed `<name>_api.h`) is
  fixed and knowable at configure time.
- **Two-layer target model**: a public `konative::kotlin::<name>` `ALIAS`/`INTERFACE` target
  wrapping a hidden `IMPORTED` target carrying the real `.a` + generated header — so the
  Kotlin/Native invocation details can change without breaking the public target name, mirroring
  both corrosion's and `KotlinNative.cmake`'s own idiom.
- **Read the NDK toolchain file's own cache variables** (`ANDROID_ABI`, `ANDROID_PLATFORM`,
  `ANDROID_NDK`) to derive the matching `kotlinc-native -target android_{arm64,arm32,x64,x86}`
  value — never re-derive ABI/API-level mapping independently.
- **CMake `FILE_SET HEADERS`** (§2) plus per-folder `CMakeLists.txt` + `add_subdirectory()` chains
  for the deep C++ header tree.

---

## 8. Project layout (the real, current tree — cross-checked against the repo itself on 2026-07-21,
not the original pre-Compose-pivot skeleton; see the note below the tree for what changed and why)

```
Konative/
├── ARCHITECTURE.md            (this document)
├── BUILDING.md                (real toolchain setup + documented workarounds, e.g. the CPM/
│                                git.cmd-shim issue)
├── README.md
├── LICENSE
├── CMakeLists.txt             (root: options, CPM bootstrap, add_subdirectory chain)
├── CMakePresets.json
├── CMakeUserPresets.json      (gitignored, machine-local — real KOTLINC/R8/ANDROID_JAR/AAPT2/JAVAC/
│                                KOTLIN_CLASSPATH_DIR/AAPT2_AAR_DIR paths live here, not in git)
├── cmake/
│   ├── CPM.cmake              (vendored — offline-reproducible per §4)
│   └── modules/
│       ├── KonativeDependencies.cmake      (every CPMAddPackage() call, pinned tags)
│       ├── KonativeAndroidToolchain.cmake  (reads ANDROID_ABI/PLATFORM, derives NDK toolchain values)
│       ├── KonativeKotlinNative.cmake      (kotlinc-native wrapper — historical, see native/ below)
│       ├── KonativeEmbedBlob.cmake         (.incbin blob embedder + build-time SHA-256, §6.5)
│       ├── KonativeGenerateIncbinAsm.cmake (emits the .S file KonativeEmbedBlob.cmake assembles)
│       ├── KonativeCompileKotlinDex.cmake  (kotlinc+Compose-compiler-plugin+aapt2+r8 pipeline, §6.6)
│       ├── KonativeEmbedKotlinDex.cmake    (drives the above, embeds both the dex AND resources.arsc
│       │                                    blobs via KonativeEmbedBlob.cmake, §6.6/§6.7)
│       └── KonativeWarnings.cmake
├── include/konative/
│   ├── core/            (Result<T,E>, assert, log, non_copyable, type_traits — detail/ inside)
│   ├── reflect/          (entt::meta registration helpers, component traits — detail/ inside)
│   ├── ecs/              (registry/world/system wrappers over entt::registry — detail/ inside)
│   ├── events/
│   │   ├── dispatcher.hpp        (the one shared entt::dispatcher wrapper — not an event itself)
│   │   ├── lifecycle/*.hpp       (one event type per file: AppStartedEvent.hpp, ...)
│   │   ├── window/*.hpp          (WindowCreatedEvent.hpp, WindowResizedEvent.hpp, ...)
│   │   └── input/*.hpp           (TouchDownEvent.hpp, KeyEvent.hpp, ...)
│   ├── scheduling/       (Taskflow / BS::thread_pool wrappers — detail/ inside)
│   ├── embed/            (checked_blob.hpp — build-time-SHA-256-verified blob loading, §6.5)
│   ├── jni/              (dex_loader.hpp/call.hpp/ref.hpp — InMemoryDexClassLoader construction
│   │                      and JNI call/ref helpers `jni_onload.cpp` uses, §6.6)
│   ├── render/           (renderer.hpp — translates window/tick events into interop calls ONLY —
│   │                      §6.2, no EGL/GLES/Vulkan header may ever appear here)
│   ├── interop/          (c_abi_export.hpp/kotlin_native_bridge.hpp — Kotlin/Native ⇄ C++ C-ABI
│   │                      boundary; historical, see §9 — no longer on this project's critical path
│   │                      now that rendering is JVM-hosted Compose, not Kotlin/Native+EGL)
│   └── app/              (application.hpp/entry_point.hpp — Application/entry-point wiring)
├── src/
│   ├── CMakeLists.txt
│   ├── README.md
│   └── platform/android/
│       ├── CMakeLists.txt
│       └── jni_onload.cpp   (the one real entry point — JNI_OnLoad → verify_blob() →
│                              load_class_from_dex() → install(Application, ByteBuffer?), §6.4; the
│                              old android_main/activity_bridge.cpp/looper_pump.cpp trio this
│                              replaced is fully REMOVED, not just superseded — confirmed by §6.7's
│                              own status table, which this tree used to silently contradict)
├── native/               (Kotlin/Native side — FULLY SUPERSEDED for rendering, kept only as a
│   ├── src/Renderer.kt    historical record per §6.7/§9; do not extend)
│   └── cinterop/         (empty by default — reserved for a genuinely un-bundled C API a future
│                          need might require, e.g. Vulkan; see native/cinterop/README.md)
├── embedded_kotlin/      (the real, CURRENT JVM/Compose UI source tree — compiled by
│   ├── README.md          KonativeCompileKotlinDex.cmake into the dex blob jni_onload.cpp loads;
│   ├── r8-rules.pro       this folder didn't exist yet when this tree was first written and is now
│   └── src/com/konative/generated/  (the single most load-bearing source tree in the project:
│                          KonativeEntryPoint.kt — the real install() entry point and Compose root;
│                          KonativeResourcesLoader.kt + KonativeResourceStringOverride.kt — the two
│                          resources.arsc runtime-gap fixes, §6.6/§6.7; KonativeResourceProvider.kt)
├── testapp/              (a real Gradle Android app with ONE real loader file —
│                          testapp/app/src/main/java/com/konative/testapp/MainActivity.kt — that
│                          calls System.loadLibrary() and nothing else; its own build.gradle.kts
│                          drives the root CMakeLists.txt end to end (CPM fetch, NDK cross-compile,
│                          Kotlin+Compose dex build, embed, packaging), §13. NOT NativeActivity/
│                          GameActivity-based, despite what an earlier version of this tree claimed.)
├── examples/
├── tests/
└── research/
    ├── research.md                         (the dex-embedding research pass — kept as prior art)
    ├── incbin_embedding_research.md         (§6.5's grounding — real .incbin/Stockfish precedent)
    └── jni_activity_bootstrap_research.md   (§6.4's grounding — ActivityLifecycleCallbacks design)
```

Every folder listed above with real content also has its own `README.md` stating that folder's
hard rules — see §12. Read the local `README.md` before adding a file to any of these folders.

**Why this tree needed rewriting**: it originally described the project's *very first* skeleton, from
before either architecture pivot (§6's own banner). It survived both pivots unchanged, so by this
rewrite it flatly contradicted §6.7's status table on multiple points at once — claiming
`include/konative/platform/android/` existed (it never does; no such directory is in the repo),
claiming `src/` still contained `activity_bridge.cpp`/`looper_pump.cpp` (removed), describing
`native/` as "owns ALL rendering" (fully superseded), and omitting `embedded_kotlin/` entirely (now
the project's main source tree). Fixed by cross-checking every entry against the real, current repo
tree rather than editing the prose in place again.

---

## 9. What's genuinely unproven vs. what has working precedent

Being explicit about this matters more than usual here, since several of these pieces have never
been combined before by anyone found in this research.

**Has real, working, citable precedent:**
- `dalvik.system.InMemoryDexClassLoader` construction from native code, and the
  `getClassLoader()`/`NewDirectByteBuffer`/reflection-`loadClass()` sequence around it — real,
  working code in `GameHub/libs/jni/src/dex_loader.cpp` (§6.6).
- GAS `.incbin` for embedding a multi-MB binary blob into a CMake-built Android `.so` — not just
  designed but **landed and verified on real arm64-v8a + x86_64 hardware** (§6.5,
  `cmake/modules/KonativeEmbedBlob.cmake`), with `official-stockfish/Stockfish`'s own NNUE-embedding
  as real precedent at a comparable size scale (`research/incbin_embedding_research.md` §4).
- `ActivityThread.currentApplication()`'s hidden-API standing — not assumed safe, directly verified
  against Google's own published `hiddenapi-flags.csv` for this project's exact API range (§6.4).
- EnTT's `meta`/`registry`/`dispatcher` individually, at scale, in real shipped projects.
- CPM.cmake vendoring for offline-reproducible builds (this workspace's own `GameHub` already does
  it).
- A near-zero-Kotlin/Java APK shell (`GameHub/testapp`'s `MainActivity` is a real, on-device-proven
  instance of exactly this shape, `System.loadLibrary()` and nothing else — Konative's own
  `testapp/` matches this shape exactly, per §6.4).
- **The full `JNI_OnLoad`-to-rendered-Compose-UI chain** (§6.4/§6.6) — moved here from the "not
  fully validated" list below; this was flagged as the framework's single largest concentration of
  unproven risk right after the Kotlin/Native+EGL pivot, since no project found combines
  native-triggered `ActivityLifecycleCallbacks` registration + runtime `ViewTree*Owner` fabrication
  + Compose-from-a-`JNI_OnLoad`-loaded-dex quite this way. It's been real, working, and repeatedly
  re-verified on real hardware since (§6.7's status table; real screenshotted rendering; this
  exact chain is what every feature landed on 2026-07-21 — lifecycle dispatch, the tick heartbeat,
  the cross-thread queue, the Taskflow self-check — builds directly on top of and depends on
  working correctly, each one independently re-confirming it does).
- **Embedded blob size with Compose's full dependency graph in the dex** — measured for real
  (2026-07-21) against the actual current build, replacing `research/research.md` §8's stale,
  pre-Compose-pivot "~2.5MB for a near-trivial Kotlin object" guess: the real `classes.dex` is
  **2,411,952 bytes** (≈2.30 MiB) and the sibling `resources.arsc` blob is **145,516 bytes**
  (≈142 KiB) — combined, ≈2.44 MiB of embedded blob data, for the full real dependency graph
  (Compose runtime/ui/foundation, activity, lifecycle-runtime/viewmodel, savedstate,
  kotlinx-coroutines-android, kotlin-stdlib). Contrary to this section's own prior "almost
  certainly undercounts" speculation, the real number lands in almost exactly the same ballpark as
  the old pre-Compose guess — R8's shrinking (still active despite `-dontoptimize`, which disables
  optimization passes, not dead-code elimination) is doing real, effective work keeping this small.
  For context, not itself part of the embedded-blob question: the real per-ABI stripped `.so` is
  ≈4.7–4.8 MB (arm64-v8a/x86_64), and the full debug universal APK (both ABIs, unstripped
  intermediate copies never included) is 13,228,309 bytes (≈12.6 MB).
- **Taskflow's real thread-spawning/scheduling machinery on Android NDK arm64-v8a** — moved here
  from the unproven list below after `konative::scheduling::run_taskflow_self_check()`
  (`include/konative/scheduling/taskflow_self_check.hpp`) actually ran, for real, on the physical
  phone (`R3GL10AHL7P`, API 36): a small parallel computation split across several real Taskflow
  tasks, verified against the mathematically-correct expected sum, not just "didn't crash." Kept as
  a permanent startup self-check (`jni_onload.cpp`'s `on_started()`), matching this framework's own
  "code checks itself" principle — real regression protection against a future NDK/toolchain
  upgrade silently breaking this, not a one-off spike thrown away after proving it once.

**Architecturally sound synthesis, partially de-risked by real prior art, still not fully
validated — prototype first:**
- **`onActivityCreated` firing for exactly the right `Activity` instance** is structurally
  guaranteed by JLS class-init ordering (§6.4), not by an Android-internals citation that could be
  pinned to an exact source line — treat as "verify once on-device," matching this project's own
  verify-empirically ethos, not an assumption to build further architecture on unverified.
- `entt::meta` combined with Boost.PFR for auto-registration, or with Glaze for
  reflection-driven JSON serialization (both philosophically clean, neither found done anywhere).
- An `entt::dispatcher` + `libcoro` "await the next event" pattern.

Treat the second list as the actual R&D risk of this project. Everything in the first list is
"assemble known-good pieces"; everything in the second list needs a real spike/prototype before
committing further architecture on top of an assumption that it works. **The very first end-to-end
milestone for this framework was exactly this: get a trivial Compose UI (a solid-color `Box`, real
`BasicText` — see §6.6/§6.7) to actually render as `MainActivity`'s content view on a connected test
device, driven entirely by `JNI_OnLoad`** — achieved and repeatedly re-verified on real hardware
long before this rewrite, proving the dex-loading mechanism, the `Application`→`Activity` handoff,
and Kotlin-owns-Compose all at once, the direct analog of the old Kotlin/Native milestone it
replaced. Kept here as a record of what that first milestone was, not as a still-pending goal.

Kotlin/Native itself (static-lib linking into an NDK CMake C++ target via `-produce static` +
`@CName`, and its real documented linking friction — JetBrains issue kotlin-native#2803, Tier-3
Android target status) is **no longer on this project's critical path** now that rendering is
JVM-hosted Compose, not Kotlin/Native+EGL — the mechanics remain real and documented if a future,
genuinely non-rendering use for Kotlin/Native ever comes up (§6.3), but nothing currently pending
in this project depends on that risk being resolved.

---

## 12. Per-folder guideline convention

Every folder under `include/konative/`, plus `src/`, `native/`, `testapp/`, `examples/`, `tests/`,
and `cmake/`, has its own `README.md` stating that folder's specific hard rules — not a repeat of
this document's general style rules (§2), but folder-local ones: what belongs here, what must
never be added here, and the one or two things a contributor gets wrong if they don't read it
first (e.g. `render/README.md`'s "no EGL/GLES/Vulkan header may ever appear here" rule, or
`events/README.md`'s "one event type, one file, no exceptions" rule). Read the local `README.md`
before adding a file to any of these folders — this document states the *architecture*; the
per-folder `README.md` states the *local law*.

---

## 13. `testapp/` and on-device verification

`testapp/` is a real, minimal Android Gradle project whose only job is packaging the `.so` the
root `CMakeLists.txt` already builds (via Gradle's `externalNativeBuild` pointing straight at that
same root `CMakeLists.txt`, not a copy) into an installable APK, so the framework can be verified
on an actual device via `adb` rather than only compiled. It owns zero application logic of its own
— see `testapp/README.md`.

Current shape (§6.4): one trivial `MainActivity : Activity()` (**not** `NativeActivity` —
superseded once rendering moved to JVM-hosted Compose, which needs a real View-hosting Activity,
something `NativeActivity` categorically cannot provide), whose companion-object `init {}` calls
`System.loadLibrary("konative_app_native")` and nothing else. Every bit of `onCreate`/content-view
behavior originates from that `.so`'s `JNI_OnLoad`, not from `MainActivity.kt` itself.

**The verification loop** (a physical device — a Galaxy-S24-class phone, `R3GL10AHL7P`, API 36,
`arm64-v8a` — plus a rooted LDPlayer x86_64 emulator as a second test device, both reachable via
`adb`/`deepadb`, matching the `android-arm64`/`android-x86_64` CMake presets respectively):

```sh
cd testapp && ./gradlew assembleDebug \
  -PkonativeNdkPath=<...> -PkonativeKotlinc=<...> -PkonativeR8=<...> -PkonativeAndroidJar=<...> \
  -PkonativeKotlinClasspathDir=<...> -PkonativeAapt2=<...> -PkonativeJavac=<...> -PkonativeAapt2AarDir=<...>
find app/build -iname "*.apk"                                        # locate the real APK - see testapp/README.md
adb install -t -r <the real APK found above>                         # -t: debug builds are testOnly
adb shell am start -n com.konative.testapp/com.konative.testapp.MainActivity
adb logcat -s Konative:V AndroidRuntime:E DEBUG:E
```

(See `testapp/README.md` for the full property reference, the `KONATIVE_EMBEDDED_DEX_PATH` manual
override for skipping the automated pipeline, and two real gotchas: where the real APK actually lands
is Gradle-version-dependent — don't hardcode either `intermediates/apk/` or `outputs/apk/`, `find`
it — and `-t` is required because AGP marks debug builds `testOnly` by default; plain `pm install -t`
over `adb shell` has also been seen to fail with `INSTALL_FAILED_MEDIA_UNAVAILABLE: Failed to
restorecon` on the LDPlayer emulator even with SELinux permissive — the client-side streamed
`adb install -t` path used above does not hit this.)

A silent `adb logcat` (no `Konative` tag, no crash) most likely means the `.so` never finished
loading — check `adb logcat *:E` for a dynamic-linker error before assuming anything about
rendering. **This loop has now been exercised end to end with the real, current, fully-automated
pipeline (real Compose UI, real AAPT2-linked resources, not a placeholder) via a real
`./gradlew assembleDebug` run, producing one universal APK containing both `arm64-v8a` and
`x86_64` native libs, installed and verified correctly on BOTH devices — including, for the first
time in this project's history, real (non-emulated) physical hardware** (2026-07-18; earlier
verification of this same pipeline had only ever run on the LDPlayer emulator or via the standalone
`cmake --preset` flow, never through a real Gradle build nor on the physical phone). See
`testapp/README.md`'s own "Verified end to end" section for the full detail.

---

## 14. Subagent orchestration rules for continued work on this repo

(Also recorded in memory as `feedback-konative-subagent-rules` — repeating here so it's visible
in-repo, not just in Claude's memory.)

- Never let a spawned subagent invoke the `deep-research` skill or spawn further subagents of its
  own — every subagent prompt must say so explicitly.
- Give each subagent multiple related sub-tasks in one long prompt, not one narrow question.
- Demand long, structured, cited output — "your final response IS the deliverable," not a summary.
- Default to launching several subagents in parallel (single message, multiple tool calls) for any
  future broad research/implementation fan-out on this project.

---

## 15. Research provenance

This document synthesizes, in order:
1. The original dex-embedding research pass (`research/research.md`) — superseded as the primary
   direction (§1), kept as prior art.
2. Direct local inspection of `GameHub`'s `KotlinNative.cmake`, `libs/events/`, `Module/Render`,
   root `README.md`/`CMakeLists.txt`.
3. Agent — EnTT deep dive (`entt::meta`/ECS/`dispatcher`, CPM integration, EnTT's own repo layout,
   compile-time cost).
4. Agent — broader C++ compile-time reflection ecosystem (CRTP/Concepts, Boost.PFR/visit_struct/
   magic_enum/Boost.Describe, C++26 P2996, Sean-Parent-style type erasure, Unreal/RTTR/Refureku
   comparison).
5. Agent — header-only project architecture prior art (EnTT/GLM/Eigen/Boost.Hana/range-v3/stb
   folder trees, `detail/` convention, PCH/unity-build/explicit-instantiation, `FILE_SET HEADERS`,
   Hazel/EventBus events-folder precedent).
6. Agent — native Android rendering + Kotlin/Native fusion (`NativeActivity`/`GameActivity`,
   EGL/Vulkan, Rust `android-activity` as the direct model, Kotlin/Native C interop and its real
   linking risk).
7. Agent — CPM.cmake and the dependency ecosystem (full API, offline-reproducibility caveats,
   CPM vs. FetchContent/Conan/vcpkg, curated library shortlist, Gradle-free NDK build workflows).
8. Agent — external concurrency/scheduling/utility libraries paired against EnTT (Taskflow vs.
   oneTBB vs. marl, concurrentqueue, libcoro, GLM alignment caveat, Glaze/cereal vs. nlohmann,
   the case against a dedicated DI library).
9. Agent — Kotlin/Native calling EGL/GLES/Vulkan directly via `cinterop` (the mechanics behind
   §6.2's rendering-moved-into-Kotlin revision), plus real-world per-module `README.md`
   conventions (§12) and minimal Gradle-driven `externalNativeBuild` test-app patterns (§13).
