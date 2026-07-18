# Konative — Architecture

> **STATUS (2026-07-17): rendering direction reversed again — read this before trusting §6
> below.** Everything in §6 (Kotlin/Native + raw EGL/GLES rendering, no JVM/no dex) describes the
> design as of earlier the same day. The user then explicitly redirected Konative's rendering to
> **JVM-hosted Jetpack Compose**, which fundamentally requires real JVM/ART (Compose cannot run on
> Kotlin/Native) — meaning **dex embedding is back** for the rendering/UI layer specifically, this
> time built as one clean, self-checking CMake+C++ framework (in the spirit of corrosion) rather
> than GameHub's own ad-hoc per-module reflection code. §6 is left intact below as a historical
> record of the design it superseded (and because the Kotlin/Native `-produce static` + `@CName`
> C-ABI mechanics it documents may still be useful for non-rendering native logic later) — do not
> implement new rendering code against §6's design. See the `project-konative-autonomous-loop`
> memory entry for the full reasoning and current status; §6 will be rewritten in place once the
> in-flight research on Compose-in-a-dex-loaded-context and the self-checking loader design lands.

This document is the synthesized design for Konative: a CMake/C++ framework combining Kotlin and
C++ into **one native Android `.so`** — rendering and app logic together. Everything below is
grounded in the six research passes summarized in §11 (plus the reversal noted above, whose own
research/provenance will be appended once its two in-flight subagents land).

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

## 6. Android integration: NativeActivity / GameActivity, EGL/Vulkan, and the Kotlin shim

### 6.1 Entry point and event loop

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
under `GameActivity`. Konative should expose this as a build-time choice (mirroring
`android-activity`'s Cargo-feature split) rather than hardcoding one.

### 6.2 Rendering lives in Kotlin/Native, not in C++ — a deliberate revision

An earlier pass of this skeleton gave Konative its own hand-written C++ EGL/GLES/Vulkan backend
(`render/backend/gles/`, `render/backend/vulkan/`). **That backend has been deleted.** It
duplicated exactly the responsibility Kotlin/Native already lets Kotlin own directly. Splitting
rendering across *two* languages (a C++ backend calling EGL, plus Kotlin/Native logic calling back
into that C++ backend) added a second interop hop for no benefit — Konative's whole premise is
that Kotlin/Native code is real native code, fully capable of calling
`eglGetDisplay`/`glClear`/`eglSwapBuffers` itself.

**A load-bearing finding that materially de-risks this**: Kotlin/Native ships pre-built cinterop
bindings for EGL, GLES2/GLES3, and core Android NDK types (`ANativeWindow`, `ANativeActivity`) for
every `androidNative*` target **out of the box** — `platform.egl`/`platform.gles2`/
`platform.gles3`/`platform.android`, sourced from
`kotlin-native/platformLibs/src/platform/android/*.def` in the JetBrains/kotlin compiler
distribution itself. No custom `.def` file, no manual NDK sysroot `-I` wiring, is needed for GLES
rendering — `native/src/Renderer.kt` just `import`s these packages directly. `native/cinterop/`
is correctly **empty** in this skeleton as a result; it's reserved for a genuinely un-bundled C
API (Vulkan is the one confirmed example — no `vulkan.def` ships, so Vulkan-from-Kotlin/Native
would need a hand-authored `.def`, with essentially zero public prior art to draw on).

There is also **real, working, published prior art** for exactly this combination:
[**natario1/Egloo**](https://github.com/natario1/Egloo), a maintained Kotlin Multiplatform library
that binds EGL+GLES for `androidNative*` targets (published `.klib` artifacts for
`androidNativeArm64`/`X64`/`Arm32`/`X86`), with real `androidNativeMain` source doing exactly the
`platform.egl`/`platform.gles2` calls Konative needs (`eglGetDisplay`, `eglInitialize`,
`eglCreateWindowSurface` from an `EGLNativeWindowType`, `eglMakeCurrent`, `eglSwapBuffers`, GLES
draw calls) — `native/src/Renderer.kt`'s own top comment points at Egloo's `egl.kt`/`EglCore.kt`
as the reference to port the config/context/surface-creation sequence from. **Important caveat**:
Egloo proves the cinterop *bindings* compile and link on Android native targets; its own demo app
is a conventional JVM Activity, not a `NativeActivity`-driven, JVM-free app — the "no JVM at all"
end-to-end render loop is still Konative's own validation to do, not something Egloo has already
proven on-device the way Konative needs it.

**Current design**: `render/renderer.hpp` (C++) does nothing but translate
`WindowCreatedEvent`/`WindowDestroyedEvent`/a per-frame tick into three flat `@CName` calls across
the interop boundary (`konative_render_on_window_created`, `konative_render_on_window_destroyed`,
`konative_render_tick` — declared in `interop/kotlin_native_bridge.hpp`). `native/src/Renderer.kt`
receives the raw `ANativeWindow*` (as a `COpaquePointer?`, `reinterpret`'d to the bundled
`platform.android.ANativeWindow` type — `EGLNativeWindowType` is literally `struct ANativeWindow*`
on Android, per the Khronos EGL spec, so no additional cast machinery is needed), owns the EGL
context/surface entirely in Kotlin, and does all `eglCreateWindowSurface`/`glClear`/
`eglSwapBuffers`-equivalent calls there. **No `EGL/`, `GLES*/`, or `vulkan/` header may ever be
included anywhere under `include/konative/`** — that's a hard rule enforced by `render/README.md`
(§12), not just a convention.

This remains the framework's biggest concentration of unproven risk (§9) — Egloo de-risks the
*binding* layer, not the *no-JVM-at-all, NativeActivity-driven* render loop Konative specifically
needs — so treat `native/src/Renderer.kt` (currently implemented only through `eglInitialize`,
with the config/context/surface-creation sequence deliberately left as a TODO rather than guessed
at) as a spike to validate, not a finished implementation, until it's actually cleared a frame on
the connected test device.

### 6.3 The Kotlin/Native ⇄ C++ boundary — and its real, documented risk

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
**Tier 3** — "not guaranteed to be tested on CI... use with caution." **Mitigation**: pin the NDK
version and the Kotlin/Native compiler version together deliberately, control symbol visibility
explicitly (`-fvisibility=hidden` + explicit exports on both sides), and budget an early spike to
prove the link works *before* designing further architecture on top of the assumption that it
does. This is the single highest-uncertainty piece of the whole framework — everything else
researched has working precedent; this specific combination does not, yet.

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

## 8. Project layout (see the skeleton actually created alongside this document)

```
Konative/
├── ARCHITECTURE.md            (this document)
├── README.md
├── CMakeLists.txt             (root: options, CPM bootstrap, add_subdirectory chain)
├── CMakePresets.json
├── cmake/
│   ├── CPM.cmake              (vendored — offline-reproducible per §4)
│   └── modules/
│       ├── KonativeDependencies.cmake   (every CPMAddPackage() call, pinned tags)
│       ├── KonativeAndroidToolchain.cmake (reads ANDROID_ABI/PLATFORM, derives Kotlin/Native target)
│       ├── KonativeKotlinNative.cmake     (kotlinc-native wrapper — mirrors GameHub's module)
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
│   ├── platform/android/ (native_app_glue / GameActivity glue, looper pump — detail/ inside)
│   ├── render/           (translates window/tick events into interop calls ONLY — §6.2, no
│   │                      EGL/GLES/Vulkan header may ever appear here)
│   ├── interop/           (Kotlin/Native ⇄ C++ C-ABI boundary — the highest-risk module, §6.3)
│   └── app/               (Application/entry-point wiring)
├── src/                  (only load-bearing .cpp: android entry point, activity_bridge.cpp/
│                          looper_pump.cpp implementing the real android_native_app_glue loop)
├── native/               (Kotlin/Native side, compiled by KonativeKotlinNative.cmake — owns ALL
│   ├── src/               rendering per §6.2, via Kotlin/Native's BUNDLED EGL/GLES/Android
│   │                      cinterop bindings — no custom .def file needed for that)
│   └── cinterop/         (empty by default — reserved for a genuinely un-bundled C API a future
│                          need might require, e.g. Vulkan; see native/cinterop/README.md)
├── testapp/              (a real Gradle Android app that loads the fused .so via NativeActivity/
│                          GameActivity for on-device adb verification — §13. Owns zero
│                          application logic itself.)
├── examples/
├── tests/
└── research/
    └── research.md       (the dex-embedding research pass — kept as prior art, not the chosen path)
```

Every folder listed above with real content also has its own `README.md` stating that folder's
hard rules — see §12. Read the local `README.md` before adding a file to any of these folders.

---

## 9. What's genuinely unproven vs. what has working precedent

Being explicit about this matters more than usual here, since several of these pieces have never
been combined before by anyone found in this research.

**Has real, working, citable precedent:**
- Kotlin/Native producing a static lib + C-ABI header, consumed via plain C linkage (JetBrains'
  own documented mechanism, if not Android-specific in JetBrains' own tutorials).
- `android_native_app_glue`/`GameActivity`'s event loop and lifecycle callback mechanics
  themselves (official NDK/AGDK samples) — the C++-side glue code in `src/platform/android/` is
  low-risk, ordinary NDK usage.
- EGL/GLES/Vulkan calls against an `ANativeWindow*` **from C/C++** (official NDK/AGDK samples) —
  the graphics-API mechanics are well-documented; what's unproven (below) is doing them from
  Kotlin/Native specifically.
- EnTT's `meta`/`registry`/`dispatcher` individually, at scale, in real shipped projects.
- CPM.cmake vendoring for offline-reproducible builds (this workspace's own `GameHub` already does
  it).
- A near-zero-Kotlin/Java APK shell (`GameHub/testapp`'s `MainActivity` is a real, on-device-proven
  instance of exactly this shape, `System.loadLibrary()` and nothing else — for JVM Kotlin, not
  Kotlin/Native, but the manifest-level mechanics carry over; Konative's own `testapp/` goes one
  step further with zero Kotlin/Java code at all, via `NativeActivity` + `hasCode="false"`).

**Architecturally sound synthesis, partially de-risked by real prior art, still not fully
validated — prototype first:**
- **Kotlin/Native calling EGL/GLES directly via its bundled `platform.egl`/`platform.gles3`
  cinterop bindings, receiving an `ANativeWindow*` handed in across the `@CName` flat-C-ABI
  boundary from C++ and `reinterpret`-ing it to `platform.android.ANativeWindow`** (§6.2/§6.3) —
  this is still the single largest concentration of unproven risk in the whole framework, but is
  now *partially* de-risked: `natario1/Egloo` is real, published, working evidence the cinterop
  bindings themselves compile/link for `androidNative*` targets. What remains genuinely unproven
  is the **no-JVM-at-all, `NativeActivity`-driven** render loop specifically — Egloo's own demo is
  a conventional JVM Activity app, not this. Vulkan-from-Kotlin/Native (no bundled bindings, would
  need a from-scratch `.def`) remains essentially unpaved with zero found prior art either way.
- Kotlin/Native static-lib linking cleanly into an NDK CMake C++ target with no libc++/symbol
  conflicts (§6.3 — the one JetBrains issue found on this exact scenario is unresolved).
- `entt::meta` combined with Boost.PFR for auto-registration, or with Glaze for
  reflection-driven JSON serialization (both philosophically clean, neither found done anywhere).
- An `entt::dispatcher` + `libcoro` "await the next event" pattern.
- Taskflow specifically on Android NDK (no confirmed track record either way — its dependency
  surface is pure `std::thread`/`std::atomic`, so risk is judged low, not zero).

Treat the second list as the actual R&D risk of this project. Everything in the first list is
"assemble known-good pieces"; everything in the second list needs a real spike/prototype before
committing further architecture on top of an assumption that it works. **The very first
end-to-end milestone for this framework should be: get `native/src/Renderer.kt` to clear the
screen a single solid color on the connected test device via `testapp/`** — that one milestone
proves the interop link, the `ANativeWindow*` handoff, and Kotlin-owns-EGL all at once.

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

Current shape: `NativeActivity` (§6.1's zero-Kotlin/Java path), `android:hasCode="false"`, one
`<meta-data android:name="android.app.lib_name">` pointing at the `konative_app_native` target
`src/platform/android/CMakeLists.txt` builds. This is deliberately the simpler of the two §6.1
options to prove first — `GameActivity`'s one-trivial-subclass requirement can be added later once
`NativeActivity` is proven working end to end.

**The verification loop** (a physical device — a Galaxy-S24-class phone, API 36, `arm64-v8a` — was
connected via `adb`/`deepadb` during this framework's development, matching the `android-arm64`
CMake preset exactly):

```sh
cd testapp && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.konative.testapp/android.app.NativeActivity
adb logcat -s konative:V AndroidRuntime:E DEBUG:E
```

A silent `adb logcat` (no `konative` tag, no crash) most likely means the `.so` never finished
loading — check `adb logcat *:E` for a dynamic-linker error before assuming anything about
rendering. This loop is the concrete, executable version of §9's "get `Renderer.kt` to clear the
screen one solid color" milestone.

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
