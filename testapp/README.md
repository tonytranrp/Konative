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
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

(A `gradle wrapper` run — or opening this folder in Android Studio once — is needed once to
generate `gradlew`/`gradle/wrapper/gradle-wrapper.jar`; neither is vendored in this skeleton.)

## The on-device verification loop

Two devices are available for this: a physical phone (`R3GL10AHL7P`, arm64-v8a, API 36) and a
**rooted** LDPlayer emulator (`adb connect 127.0.0.1:5555`, x86_64, API 34 — root access makes it
the better first choice for debugging a failed load, since `run-as`/direct `/data` access work
without restriction there).

```sh
adb -s <device-serial> install -r app/build/outputs/apk/debug/app-debug.apk
adb -s <device-serial> shell am start -n com.konative.testapp/.MainActivity
adb -s <device-serial> logcat -s Konative:V AndroidRuntime:E ActivityManager:E   # confirm the .so loaded and the embedded module initialized
adb -s <device-serial> exec-out screencap -p > konative_test.png                 # confirm the Compose UI actually rendered
```

If `adb logcat` shows no `Konative` tag and no crash, the `.so` most likely failed to load, or
loaded but failed to construct the `InMemoryDexClassLoader` (the framework's own self-check should
report this clearly rather than silently swallowing a JNI exception — see `ARCHITECTURE.md`'s
self-checking-loader design). Check `AndroidRuntime:E`/`ActivityManager:E` for an
`UnsatisfiedLinkError` (native link failure) versus a caught-and-logged dex/classloader error
(framework-level failure, should be self-reported) to tell the two failure modes apart. On the
rooted LDPlayer emulator, `adb root_shell` access lets you inspect `/data/data/com.konative.testapp/`
directly if logcat alone isn't enough to diagnose a failure.
