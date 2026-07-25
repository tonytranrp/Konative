# Konative

[![Desktop tests](https://github.com/tonytranrp/Konative/actions/workflows/desktop-tests.yml/badge.svg)](https://github.com/tonytranrp/Konative/actions/workflows/desktop-tests.yml)
[![Android build](https://github.com/tonytranrp/Konative/actions/workflows/android-build.yml/badge.svg)](https://github.com/tonytranrp/Konative/actions/workflows/android-build.yml)

Konative fuses Kotlin (rendered with Jetpack Compose) and C++ into a single native Android `.so`. Your app's own build embeds real, compiled Kotlin+Compose bytecode as linked data inside the `.so`; at load time, the `.so`'s own `JNI_OnLoad` verifies it (a build-integrity self-check, SHA-256 over the embedded blob) and loads it via `dalvik.system.InMemoryDexClassLoader`, so your Compose UI runs as real JVM code inside your app's already-live ART process — no separate JVM to bootstrap, no `NativeActivity`, no raw EGL/GLES. On the Java/Kotlin side, this is deliberately as close to invisible as `System.loadLibrary()` gets: the entire app-side `.kt` footprint is one loader file. It's the Corrosion-for-Cargo idea applied to this pipeline: a first-party CMake layer (`kotlinc`+Compose-compiler-plugin+`aapt2`+`r8`+`.incbin`-embed) that wraps the real toolchain end to end, rather than a one-off hand-rolled build script.

The other half of Konative is what actually runs behind that Compose UI: a header-only, EnTT-based ECS/events/scheduling core living in `include/konative/` — the same shape of foundation you'd expect under a game or simulation engine, not just a UI shell. The two halves meet at the `JNI_OnLoad` boundary: real C++ systems tick on every real display frame, and the results feed straight into the composables actually on screen (see "Architecture, in shape" below).

**Status.** This is a framework proving its own architecture, not a shipped app and not a published library. Every mechanism described below is real and has actually been built and run — verified by a real hosted CI matrix (both badges above: a desktop doctest suite on every push/PR, and an Android leg that both cross-compiles the two Android presets *and* boots a real hardware-accelerated x86_64 emulator, installs the built APK, and fails the job if logcat shows a crash or any of the app's own on-device self-checks report `FAILED`), by day-to-day manual verification on a rooted x86_64 emulator, and — as of 2026-07-18, for the first time in this project's history — on real, non-emulated `arm64-v8a` physical hardware. There is still no versioned release and no Maven/Gradle package (see "Using Konative in your own project" below), and some pieces documented in `ARCHITECTURE.md` — an earlier Kotlin/Native-based rendering design, most of `include/konative/interop/` and `include/konative/render/` — are deliberately dormant: kept because still-existing code cites their mechanics, not because they're on the current critical path. `ARCHITECTURE.md`'s own status banner and its section 9 ("What's genuinely unproven vs. what has working precedent") give the full, dated, itemized account this README only summarizes.

## Why Konative exists

The concrete question Konative exists to answer: can a real Jetpack Compose UI run as real, compiled JVM bytecode *inside* a native Android `.so`, with zero OpenGL/EGL/Vulkan anywhere, zero separate JVM process to bootstrap, and an app-side Kotlin/Java footprint reduced to one loader file? That's not a hypothetical here — every piece of the answer (the `.incbin` blob embedder, the build-time SHA-256 self-check, the `JNI_OnLoad` bridge, the `kotlinc`+Compose-compiler-plugin+`aapt2`+`r8` CMake pipeline, the real Compose UI it produces) is built and has actually rendered on a physical device, not just compiled cleanly.

This wasn't the original plan, and it's worth stating that honestly rather than glossing over it. Konative's first design (`research/research.md`, `ARCHITECTURE.md` section 1) compiled Kotlin *ahead of time* via the Kotlin/Native compiler straight into machine code linked into the `.so` — no JVM, no dex, no runtime reflection at all, with C++ templates and EnTT-driven compile-time reflection doing the work a dex-embedding approach would otherwise need JNI+ART for. That design is still real, still described in full (`ARCHITECTURE.md` sections 6.1–6.3), and still the load-bearing history behind code that still exists (`native/`, `include/konative/interop/`) — it's kept, not deleted, because it's still individually accurate documentation for that code. It was reversed for rendering specifically, for one concrete reason: Jetpack Compose fundamentally needs a real JVM/ART object model, which Kotlin/Native categorically cannot provide. So dex-embedding came back for the UI layer — but built as one clean, self-checking CMake+C++ pipeline this time, rather than the ad hoc, per-module reflection code the project this design draws prior art from (internally referred to as `GameHub`) uses.

The other reason Konative exists is the C++ side of that fusion. Rather than writing ordinary application logic in Kotlin and treating C++ as a thin performance escape hatch, Konative puts a real EnTT-based ECS — entities, components, a single generic event dispatcher, Taskflow/thread-pool-backed scheduling, GLM-based spatial transforms, cereal-based snapshot persistence, Glaze-based JSON config with live hot-reload — as the actual application/simulation layer, with heavy compile-time metaprogramming (CRTP, C++20 concepts, zero-codegen `entt::meta` reflection) doing the structural work. That's a real, opinionated design choice (`ARCHITECTURE.md` section 2 states the coding-style rules this implies), not the only way to combine C++ and Android, and not a claim that it's the *right* way for every project — it's what this project is actually exploring.

## Architecture, in shape

The pieces, and how they actually fit together:

1. **The C++ ECS core — `include/konative/`.** Header-only-first (`.hpp` carries both declarations and definitions; `.cpp` is reserved for genuinely load-bearing entry points — see `src/README.md`). Built around `entt::registry`, wrapped by `ecs/` (`Registry`, and `World` — one `Registry` + one `SystemSequence` + one event `Dispatcher` per app instance), `events/` (a single shared `dispatcher.hpp`, plus one `.hpp` per event type under `lifecycle/`, `window/`, `input/`, `persistence/`), `reflect/` (`entt::meta` registration helpers, Boost.PFR auto-registration, Glaze JSON (de)serialization of reflected components), `scheduling/` (Taskflow, `BS::thread_pool`, and `concurrentqueue`/`readerwriterqueue`-backed cross-thread and SPSC event queues), `spatial/` (a GLM-based `Transform` plus `approach()` — the actual ECS-side math GLM was pulled into the dependency stack for), `embed/` (the runtime SHA-256 self-check counterpart to the CMake blob embedder below), `app/` (the `Application` base class, the one-per-binary `create_application()` contract, and a Glaze-backed `AppConfig` with file-backed hot-reload), and, Android-only, `jni/` (the `InMemoryDexClassLoader` construction plus JNI call/ref helpers the bridge below uses). `include/konative/render/` and `include/konative/interop/` still exist but are historical — see the Status note above.
2. **The CMake pipeline — `CMakeLists.txt` and `cmake/`.** `cmake/CPM.cmake` is vendored in-tree, not downloaded fresh at configure time, so a cold clone plus a warm `CPM_SOURCE_CACHE` can reconfigure fully offline; `cmake/modules/KonativeDependencies.cmake` pins every third-party dependency (EnTT, GLM, Taskflow, `BS::thread_pool`, `concurrentqueue`/`readerwriterqueue`, cereal, Glaze, doctest, PicoSHA2, and, non-Android only, libcoro). Two Konative-specific modules do the actual fusing: `KonativeEmbedBlob.cmake` embeds an arbitrary file as linked read-only data via a GAS `.incbin` directive plus a build-time SHA-256 (chosen over generating a giant C-array source file, which chokes a compiler's lexer past a few MB — see `research/incbin_embedding_research.md`), and `KonativeCompileKotlinDex.cmake`/`KonativeEmbedKotlinDex.cmake` drive `kotlinc` + the Compose compiler plugin + `aapt2` + `r8` end to end, turning a Kotlin source tree into a shrunk `classes.dex` plus a real, AAPT2-linked `resources.arsc`, then hand both to the blob embedder above.
3. **`embedded_kotlin/` — the actual Kotlin/Compose source.** What step 2 above actually compiles — never Gradle. `KonativeEntryPoint.kt` is the real `install(Application, ByteBuffer?)` entry point and Compose root; `KonativeResourceProvider.kt`, `KonativeResourcesLoader.kt`, and `KonativeResourceStringOverride.kt` are two layered fixes for a real gap (code loaded via `InMemoryDexClassLoader` has no JAR/ZIP resource backing at all, so anything ultimately backed by `Resources.getString()` doesn't resolve without extra work — see `embedded_kotlin/README.md`'s dated update history for the full account of that bug and its fix).
4. **The bridge — `src/platform/android/jni_onload.cpp`.** The one native entry point for the app target, replacing an earlier `android_native_app_glue`/`NativeActivity`-based design that's now fully deleted (not just superseded). `JNI_OnLoad` runs before any Activity code exists, verifies the embedded blob's SHA-256, constructs the `InMemoryDexClassLoader` (with a resource-aware parent classloader working around the gap in step 3), and makes exactly one `CallStaticVoidMethod` handoff into `KonativeEntryPoint.install(...)`. Everything past that point is real compiled Kotlin, not further JNI reflection. A handful of `RegisterNatives` bindings close the loop the other way: a `Choreographer.FrameCallback` on the Kotlin side calls a native tick function once per real display frame, driving the C++ `World`; touch/key/window events flow from Compose back into the C++ event `Dispatcher`; and the Compose UI reads live C++ state (a tick counter, a touch-following dot's live position) back out through native getters.
5. **`testapp/` — the reference Android app.** A real Gradle project whose entire Kotlin footprint is one file, `testapp/app/src/main/java/com/konative/testapp/MainActivity.kt`:
   ```kotlin
   class MainActivity : Activity() {
       companion object { init { System.loadLibrary("konative_app_native") } }
   }
   ```
   No `onCreate()` override, nothing else — every bit of real behavior originates from the `.so`'s own `JNI_OnLoad`. Its `app/build.gradle.kts` drives the root `CMakeLists.txt` above via Gradle's ordinary `externalNativeBuild`, exactly like any Android NDK app, just pointed at this repository's own root build instead of a project-local one.

For the full design — every JNI signature, the exact `.incbin`/SHA-256 mechanics, the independently-diagnosed bugs the dex-loading path needed fixed, and a dated, itemized table of what's landed versus still open — see `ARCHITECTURE.md`, especially section 6 (the `JNI_OnLoad`/dex mechanism in full), section 8 (the complete, annotated project tree), section 9 (unproven vs. proven, with real caveats named explicitly), and section 11 (the on-device verification loop).

## Project layout

```
Konative/
├── ARCHITECTURE.md        full design reference (~100KB — the deep documentation this file links to)
├── BUILDING.md            full build instructions, all three presets, real reproduced troubleshooting
├── CMakeLists.txt         root build: options, CPM bootstrap, add_subdirectory chain
├── CMakePresets.json      desktop-debug / android-arm64 / android-x86_64
├── cmake/                 CPM.cmake (vendored) + the KonativeEmbed*/KonativeCompile* pipeline modules
├── include/konative/      the C++ ECS/events/reflection/scheduling core (header-only)
├── src/                   the only .cpp translation units in the framework — platform/android/jni_onload.cpp
├── embedded_kotlin/       the real Kotlin+Compose UI, compiled by cmake/'s pipeline, never by Gradle
├── native/                the earlier Kotlin/Native rendering path — superseded, kept as history only
├── testapp/               the reference Android app: one loader Activity + a real Gradle wrapper
├── tools/kotlin-classpath-resolver/   a standalone Gradle project resolving the AndroidX/Compose
│                                       classpath the CMake pipeline needs (CMake has no Maven-aware
│                                       dependency resolver of its own)
├── examples/              small, desktop-buildable examples (minimal_triangle)
├── tests/                 the doctest suite desktop-debug builds and runs
└── research/              the research passes ARCHITECTURE.md's design is grounded in
```

Nearly every folder with real content has its own `README.md` stating that folder's specific hard rules (`ARCHITECTURE.md` section 10) — for example `include/konative/app/README.md`, `include/konative/ecs/README.md`, `include/konative/spatial/README.md`, `src/README.md`, `embedded_kotlin/README.md`, and `testapp/README.md`. Reading the local `README.md` before touching a folder is genuinely the fastest way to find the constraint that would otherwise bite you — this is a convention the project holds itself to, not busywork suggested only for newcomers.

## Prerequisites

**To build and run the C++ core's own tests — no Android SDK, no NDK, no Kotlin toolchain at all:**

- A real `git` executable. On Windows specifically, make sure `git` on your `PATH` resolves to Git for Windows' own `git.exe`, not an npm-installed `git.cmd` shim — CPM's git-based fetch steps fail under a `.cmd` wrapper (see the troubleshooting entry in `BUILDING.md`).
- CMake 3.23 or newer — the floor both `CMakeLists.txt` and `CMakePresets.json` declare (desktop CI itself pins CMake ~3.28).
- Ninja — all three configure presets (`desktop-debug`, `android-arm64`, `android-x86_64`) hardcode the Ninja generator.
- A C++20 compiler. This repository has actually been built with Clang (via an llvm-mingw distribution, on the maintainer's own Windows machine) and with GCC (Ubuntu, in the hosted runner `desktop-tests.yml` uses).

**Additionally, to build the real Android app** (`testapp/`, or your own project structured the same way — see below):

- A real Android SDK (`ANDROID_HOME`, or a `local.properties` with `sdk.dir=...`).
- A real Android NDK — r28 is the version this project's own on-device verification and CI have actually used; the `android-arm64`/`android-x86_64` presets target `ANDROID_PLATFORM android-26` (Android 8.0+).
- A JVM-targeting `kotlinc` distribution. This is a genuinely different compiler distribution from `kotlinc-native` (used only by the dormant Kotlin/Native path mentioned above) — the real, current pipeline never invokes `kotlinc-native` at all.
- A JDK (`javac`), for the AAPT2-linked resource-class compilation step.
- `r8` and `aapt2` — both normally resolved from an installed Android SDK's `build-tools/<version>/` directory; Konative's own CMake pipeline can auto-discover both (and `KONATIVE_ANDROID_JAR`/`KONATIVE_JAVAC`) from `ANDROID_HOME`/`ANDROID_SDK_ROOT` if left unset (`ARCHITECTURE.md` section 6.6) — only the `kotlinc` path has no equivalent auto-discovery convention and normally needs to be set explicitly.
- A directory of pre-resolved AndroidX/Compose/coroutines dependency jars, and a matching directory of the real, unmodified `.aar` files for that same set — CMake has no Maven-aware dependency resolver of its own. Either assemble these by hand, or run the real Gradle project at `tools/kotlin-classpath-resolver/` (see that folder's own `README.md`), which resolves and exports both.
- Gradle, for the Android app itself — a real, committed Gradle wrapper (pinned to 9.4.1) is vendored in `testapp/`, so no separate system-wide Gradle install is required.

None of the second list is needed just to clone the repo and run its own test suite — that's the entire point of the `desktop-debug` preset below.

## Getting the code

```sh
git clone https://github.com/tonytranrp/Konative.git
cd Konative
```

## Quick start: build and run the C++ core's own tests

The fastest way to see any of this actually working, and the one path that needs nothing Android- or Kotlin-related at all:

```sh
cmake --preset desktop-debug
cmake --build build/desktop-debug
./build/desktop-debug/tests/konative_tests.exe
```

(On Linux/macOS, drop the `.exe` suffix — that's exactly what `desktop-tests.yml`'s own CI step runs: `./build/desktop-debug/tests/konative_tests`.)

This is the same preset and the same commands `BUILDING.md`'s own Quick Start documents — originally verified end to end on 2026-07-17, and re-confirmed directly while writing this: on this machine, right now, that binary reports `75 test cases, 252 assertions, all passed`. It's also run on every push/PR to `main` by the "Desktop tests" badge at the top of this file.

What this actually builds and proves: `desktop-debug` compiles all of `include/konative/`, then (since `KONATIVE_BUILD_TESTS`/`KONATIVE_BUILD_EXAMPLES` both default on for a top-level configure) the real `konative_tests` doctest suite — 20 `test_*.cpp` files as of this writing (`tests/CMakeLists.txt`), covering `core::Result`, the ECS `World`/`Registry`, event dispatch, `entt::meta` reflection (plus Boost.PFR auto-registration and Glaze JSON), the embedded-blob SHA-256 self-check, the cross-thread and SPSC event queues, the Taskflow and `BS::thread_pool` self-checks, GLM-through-EnTT-storage, cereal registry snapshot/restore, file-backed `AppConfig` hot-reload, and the `spatial::Transform` module — plus `examples/minimal_triangle`, a small desktop-buildable `Application` subclass proving the ECS/events/app lifecycle runs cleanly start to finish (it doesn't actually render a triangle or anything else, despite the name — see `examples/README.md`). None of this touches Android, JNI, or Compose; it only proves the C++ core itself.

If step 1 fails with `fatal: ambiguous argument 'HEAD0'`, that's the git.cmd-shim issue mentioned above — see `BUILDING.md` for the fix (`-DGIT_EXECUTABLE=...`) and its other real, previously-hit build issues (a CMake-version floor needed for one CPM dependency, an `fmt`/`spdlog`-vs-new-Clang `consteval` incompatibility, a bad CPM `GIT_TAG`, and more), each recorded with its real root cause, not just its fix.

## Building the real Android app

The C++ core above compiles and tests entirely on desktop. The fused `.so` with the real embedded Compose UI, and an installed, on-device-verified APK, is a separate, longer path — documented in full in `BUILDING.md` (the two Android CMake presets, which need a real NDK wired through a machine-local, gitignored `CMakeUserPresets.json`) and `testapp/README.md` (the real Gradle recipe, the full meaning of every `konative*` property, and the on-device verification loop this project actually uses: a physical `arm64-v8a` phone plus a rooted x86_64 emulator).

The short version, condensed from `testapp/README.md`:

```sh
cd testapp
./gradlew assembleDebug \
  -PkonativeNdkPath=<path to an installed NDK> \
  -PkonativeKotlinc=<path to kotlinc(.bat)> \
  -PkonativeR8=<path to r8(.bat)> \
  -PkonativeAndroidJar=<path to android.jar> \
  -PkonativeKotlinClasspathDir=<path to a pre-resolved dependency-jar directory> \
  -PkonativeAapt2=<path to aapt2(.exe)> \
  -PkonativeJavac=<path to javac(.exe)> \
  -PkonativeAapt2AarDir=<path to a directory of the real, unmodified .aar files for the same set>
adb install -t -r <the real APK — see "Where the APK actually lands" in testapp/README.md>
```

This drives the exact same root `CMakeLists.txt` the desktop path above does, cross-compiled for Android, with `konative_embed_kotlin_dex()` compiling `embedded_kotlin/`'s real Compose UI via `kotlinc`+the Compose compiler plugin+`aapt2`+`r8` and embedding the result directly into `konative_app_native`'s `.so` at build time. See `testapp/README.md` for what each property means, the `KONATIVE_EMBEDDED_DEX_PATH` manual override (for a machine with no Kotlin toolchain installed at all), and the full, dated verification history — including the 2026-07-18 run that, for the first time, put this whole pipeline on real physical hardware.

## Using Konative in your own project

There is no published Konative package today: no Maven Central or JitPack coordinate, no Gradle `implementation("...")` line, no CMake `find_package(Konative)` against a pre-built install. The only way to consume Konative right now is the pattern `testapp/` itself already demonstrates (its own Hard Rules, in `testapp/README.md`):

1. **One loader Activity, one `.kt` file, one job.** Your app owns exactly one Kotlin file, whose entire body constructs a class that calls `System.loadLibrary("<your-target-name>")` from an `init {}` block (`testapp/`'s own `MainActivity.kt`, quoted in full above, is four lines — see `ARCHITECTURE.md` section 6.4 for exactly why this is guaranteed to run before `onCreate()`). It never overrides `onCreate()` or calls `setContentView()` — everything past that point is native code's job.
2. **Your own `app/build.gradle.kts` points `externalNativeBuild.cmake.path` straight at Konative's own root `CMakeLists.txt`** — never a copy of it — the same way `testapp/`'s does. This is what actually cross-compiles the C++ core, drives the CPM dependency fetch, and, via `konative_embed_kotlin_dex()`, compiles and embeds the Kotlin/Compose UI, all through Gradle's own `externalNativeBuild` mechanism.
3. **The loader's `System.loadLibrary(...)` argument must match the shared-library target name** that `src/platform/android/CMakeLists.txt` actually declares (`konative_app_native` today — Android strips the `lib` prefix/`.so` suffix automatically).
4. **Your real UI/app logic is a separate Kotlin source tree, compiled by Konative's own CMake pipeline, never by Gradle's Kotlin/AGP plugin.** The Kotlin Android Gradle plugin in your `build.gradle.kts` exists only to compile the one loader file from step 1 — it must never be pointed at your real UI code. That's what `embedded_kotlin/` is today: a plain Kotlin source tree that `konative_embed_kotlin_dex()` compiles with `kotlinc` plus the Compose compiler plugin, links resources with `aapt2`, shrinks with `r8`, and embeds as a `classes.dex`+`resources.arsc` pair directly into the `.so` — never packaged as a second, Gradle-built dex the APK ships separately.
5. **You'll need the same seven `konative*`/`KONATIVE_*` toolchain paths** `testapp/README.md` documents (`konativeNdkPath`, `konativeKotlinc`, `konativeR8`, `konativeAndroidJar`, `konativeKotlinClasspathDir`, `konativeAapt2`, `konativeJavac`, `konativeAapt2AarDir` — or the matching env vars/CMake cache variables), plus optionally `konativeGitExecutable` if you hit the same git.cmd-shim issue on Windows.

**One honest wrinkle worth being direct about, rather than leaving implicit**: the pattern above is real, but as of this writing it isn't yet parameterized for a genuinely separate consumer project to plug its own app logic into an unmodified Konative checkout. `src/platform/android/CMakeLists.txt` compiles exactly one C++ entry point (`jni_onload.cpp`, which already contains a concrete `create_application()` implementation — the current demo, `KonativeAndroidApp`) and globs exactly one Kotlin source directory (`embedded_kotlin/src/`, a path relative to this repository's own tree) into `konative_app_native`. `testapp/` proves the *packaging* pipeline works end to end — Gradle to CMake to a fused `.so` with a real, rendering embedded Compose UI to an installed, on-device-verified APK — using Konative's own existing demo content (the ECS heartbeat counters and the touch-following dot described in `ARCHITECTURE.md`'s status table), not by supplying different app logic of its own. So the real, current way to build your own app with Konative is to work inside a clone (or fork) of this repository — replacing `embedded_kotlin/src/com/konative/generated/`'s Compose UI and `jni_onload.cpp`'s `create_application()` implementation with your own — and point your own thin loader-Activity project's Gradle build at that clone, exactly as described above. It is not yet "add Konative as a dependency and configure it from the outside" against a pristine, unmodified upstream checkout — that would be a real, separate piece of framework work this repository hasn't done yet.

## Where to go next

- **`ARCHITECTURE.md`** — the full design: every JNI signature and the complete `JNI_OnLoad`/embedded-dex mechanism (section 6), the EnTT-based reflection/ECS/events/scheduling design (sections 2–5), the CPM-managed dependency stack (section 4), the CMake pipeline in full (section 7), the complete annotated project tree (section 8), a dated, itemized account of what's landed versus still open (section 9), and the on-device verification loop (section 11).
- **`BUILDING.md`** — full build instructions for all three presets, plus every real, previously-hit build issue and its actual fix (the git.cmd shim, a CMake-version floor, an `fmt`/`spdlog`-vs-new-Clang incompatibility, a bad CPM `GIT_TAG`, and more).
- **`testapp/README.md`** — the real reference Android app: the full `konative*` Gradle-property reference, where the built APK actually lands (which is Gradle-version-dependent), and the real on-device verification loop across a physical phone and a rooted emulator.
- **`research/`** — the research this design is grounded in: `research/research.md` (the original dex-embedding research pass), `research/incbin_embedding_research.md` (the `.incbin`/GAS-directive mechanics behind the blob embedder), and `research/jni_activity_bootstrap_research.md` (the `ActivityLifecycleCallbacks`/`ViewTree*Owner`-fabrication design behind the `JNI_OnLoad` bridge).
- **Per-folder `README.md` files** — every module under `include/konative/`, plus `src/`, `embedded_kotlin/`, `tools/kotlin-classpath-resolver/`, `examples/`, and `tests/`, has its own `README.md` stating that folder's hard rules. Read the local one before changing anything inside it.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE).
