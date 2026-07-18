# testapp/

The real APK that loads the fused `.so` (`konative_app_native`, built from the repo root
`CMakeLists.txt` via Gradle's `externalNativeBuild`) on an actual device — the on-device
verification loop for everything in `ARCHITECTURE.md`, not a place for application logic.

**Status (2026-07-17, autonomous-loop rework in progress)**: this app's shape just changed. It is
no longer a zero-Kotlin `NativeActivity`. Konative's rendering direction reversed to a JVM-hosted
Jetpack Compose UI, dex-embedded in the `.so` and loaded via `InMemoryDexClassLoader` — see the
`project-konative-autonomous-loop` memory entry / `ARCHITECTURE.md` for the full reasoning. That
means `testapp/` needs a real `Activity`, not the framework-provided `android.app.NativeActivity`.

## Hard rules for this folder

- **This module owns exactly one `.kt` file, `MainActivity.kt`, and its only job is
  `System.loadLibrary("konative_app_native")`** — matching GameHub's own `testapp/
  MainActivity.java` pattern exactly, just ported to Kotlin. Every line of real behavior lives in
  `include/konative/**.hpp` (C++) or the dex-embedded Kotlin/Compose module the `.so` loads at
  runtime. If you find yourself wanting to add a second `.kt`/`.java` file here, that logic
  belongs in the embedded module instead — this app's whole point is proving the *framework*
  produces a working `.so`, not building a bespoke test harness.
- **`app/build.gradle.kts`'s `externalNativeBuild.cmake.path` must keep pointing at the repo
  root `CMakeLists.txt`**, never a copy — this module drives the exact same CMake build
  `ARCHITECTURE.md §7` documents, just via Gradle instead of a standalone `cmake --preset`.
- **`MainActivity.kt`'s `System.loadLibrary(...)` argument must match `src/platform/android/
  CMakeLists.txt`'s shared-library target name exactly** (Android strips the `lib` prefix/`.so`
  suffix automatically — the argument is `konative_app_native`, the built artifact is
  `libkonative_app_native.so`).
- **The Kotlin Android Gradle plugin in `app/build.gradle.kts` exists ONLY to compile this one
  loader file.** It must never be used to compile the real Compose UI code — that code is
  compiled by a separate, hand-rolled `kotlinc`+`d8` CMake pipeline and embedded into the `.so`
  directly (this is the whole point of the framework — see `ARCHITECTURE.md`), not built by
  Gradle's normal Kotlin/AGP pipeline into a second dex file the APK ships separately.

## Building and installing

```sh
cd testapp
./gradlew assembleDebug \
  -PkonativeNdkPath=<path to an installed NDK> \
  -PkonativeKotlinc=<path to kotlinc(.bat)> \
  -PkonativeR8=<path to r8(.bat)> \
  -PkonativeAndroidJar=<path to android.jar> \
  -PkonativeKotlinClasspathDir=<path to a pre-resolved dependency-jar directory>
adb install -t -r app/build/intermediates/apk/debug/app-debug.apk
```

(A `gradle wrapper` run — or opening this folder in Android Studio once — is needed once to
generate `gradlew`/`gradle/wrapper/gradle-wrapper.jar`; neither is vendored in this skeleton.)

**The `kotlinc`+Compose-compiler-plugin+`r8` CMake pipeline that produces the embedded dex is now
automated** (`cmake/modules/KonativeEmbedKotlinDex.cmake`, `ARCHITECTURE.md` section 6.6) — the
four `konative*`-prefixed properties above forward straight through to the matching CMake cache
variables (`KONATIVE_KOTLINC`/`KONATIVE_R8`/`KONATIVE_ANDROID_JAR`/`KONATIVE_KOTLIN_CLASSPATH_DIR`,
same machine-local-override pattern as `konativeNdkPath` below), which is what `src/platform/
android/CMakeLists.txt` uses by default now. Each can also be set via the matching env var
(`KONATIVE_KOTLINC`, `KONATIVE_R8`, `KONATIVE_ANDROID_JAR`, `KONATIVE_KOTLIN_CLASSPATH_DIR`) instead
of a Gradle property. `KONATIVE_KOTLIN_CLASSPATH_DIR` must be a directory of pre-resolved
dependency jars (Compose runtime/ui/foundation, activity, lifecycle-runtime/viewmodel, savedstate,
kotlinx-coroutines-android — NOT kotlin-stdlib.jar, that's sourced automatically from the kotlinc
distribution itself) — see `embedded_kotlin/README.md` for how this directory is currently
produced; real Maven dependency resolution from CMake is still an open problem.

`-PkonativeEmbeddedDexPath=<path to a real classes.dex>` (or the `KONATIVE_EMBEDDED_DEX_PATH` env
var) remains available as a manual override — set it to skip the automated pipeline entirely and
embed a hand-built or pre-built dex instead (containing a
`com.konative.generated.KonativeEntryPoint` class with a `@JvmStatic install(Application)` static
method, which `src/platform/android/jni_onload.cpp` calls). Useful on a machine without kotlinc/r8
installed at all.

One more optional `-P`/env-var override exists for the same reason `CMakeUserPresets.json` exists
for the standalone `cmake --preset` flow (see `BUILDING.md`) — a machine-local escape hatch that
never gets committed as a hardcoded path:

- `konativeGitExecutable` / `KONATIVE_GIT_EXECUTABLE` — `BUILDING.md`'s `-DGIT_EXECUTABLE=...`
  git.cmd-shim workaround. A bare `-DGIT_EXECUTABLE=...` on the `gradle` command line does **not**
  reach the CMake invocation AGP builds internally (that `-D` sets a JVM system property on the
  Gradle daemon, nothing more) — it must go through this property instead.

(`konativeNdkPath` / `KONATIVE_NDK_PATH` — points AGP directly at an already-installed NDK,
bypassing its SDK-manager-based auto-provisioning, which needs both a one-time interactive
`sdkmanager --licenses` run AND write access to the SDK's own install directory — fails outright
if the SDK lives somewhere admin-only, e.g. under `Program Files`.)

## The on-device verification loop

Two devices are available for this: a physical phone (`R3GL10AHL7P`, arm64-v8a, API 36) and a
**rooted** LDPlayer emulator (`adb connect 127.0.0.1:5555`, x86_64, API 34 — root access makes it
the better first choice for debugging a failed load, since `run-as`/direct `/data` access work
without restriction there).

```sh
adb -s <device-serial> install -t -r app/build/intermediates/apk/debug/app-debug.apk
adb -s <device-serial> shell am start -n com.konative.testapp/.MainActivity
adb -s <device-serial> logcat -s Konative:V AndroidRuntime:E ActivityManager:E   # confirm the .so loaded and the embedded module initialized
adb -s <device-serial> exec-out screencap -p > konative_test.png                 # confirm the Compose UI actually rendered
```

`app/build/intermediates/apk/debug/` (not the classic `app/build/outputs/apk/debug/`, which this
AGP/Gradle version only leaves a redirect-pointer file in) is where the real APK actually lands —
verified by building it for real, not assumed. `-t` is required: AGP marks debug builds `testOnly`
by default and plain `adb install -r` refuses them. Prefer the client-side `adb install` shown
above over `adb shell pm install` — the latter has been seen to fail with
`INSTALL_FAILED_MEDIA_UNAVAILABLE: Failed to restorecon` on the LDPlayer emulator even with SELinux
already permissive, while the streamed `adb install -t` path does not hit this.

If `adb logcat` shows no `Konative` tag and no crash, the `.so` most likely failed to load, or
loaded but failed to construct the `InMemoryDexClassLoader` (the framework's own self-check should
report this clearly rather than silently swallowing a JNI exception — see `ARCHITECTURE.md`'s
self-checking-loader design). Check `AndroidRuntime:E`/`ActivityManager:E` for an
`UnsatisfiedLinkError` (native link failure) versus a caught-and-logged dex/classloader error
(framework-level failure, should be self-reported) to tell the two failure modes apart. On the
rooted LDPlayer emulator, `adb root_shell` access lets you inspect `/data/data/com.konative.testapp/`
directly if logcat alone isn't enough to diagnose a failure.

**Verified end to end** (2026-07-18, LDPlayer x86_64 emulator), with the fully automated pipeline —
no `-PkonativeEmbeddedDexPath` override, no hand-built dex: `./gradlew assembleDebug` with only the
four toolchain properties above compiled `embedded_kotlin/` via `kotlinc`+r8 at build time,
installed the resulting APK, launched it, and confirmed via logcat + screenshot that `JNI_OnLoad`
ran, the embedded dex blob passed its SHA-256 self-check, `KonativeResourceProvider`'s opportunistic
upgrade succeeded, `KonativeEntryPoint.install(Application)` executed, and the real Jetpack Compose
UI rendered — the same green-`Box`-plus-white-"Konative"-text output as the original hand-built
milestone (`ARCHITECTURE.md` §6.6/6.7), with zero manual dex-building steps.
