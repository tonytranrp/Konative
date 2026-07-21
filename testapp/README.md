# testapp/

The real APK that loads the fused `.so` (`konative_app_native`, built from the repo root
`CMakeLists.txt` via Gradle's `externalNativeBuild`) on an actual device — the on-device
verification loop for everything in `ARCHITECTURE.md`, not a place for application logic.

**Status (2026-07-18, updated)**: landed and verified on real hardware, not in progress. This app is
no longer a zero-Kotlin `NativeActivity` — Konative's rendering direction reversed to a JVM-hosted
Jetpack Compose UI, dex-embedded in the `.so` and loaded via `InMemoryDexClassLoader` — see the
`project-konative-autonomous-loop` memory entry / `ARCHITECTURE.md` for the full reasoning. That
means `testapp/` needs a real `Activity`, not the framework-provided `android.app.NativeActivity` —
see the Hard rules below for its exact (minimal) shape, and "Verified end to end" further down this
file for the real, screenshotted, on-device proof.

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
  compiled by a separate, automated `kotlinc`+`r8` CMake pipeline and embedded into the `.so`
  directly (this is the whole point of the framework — see `ARCHITECTURE.md`), not built by
  Gradle's normal Kotlin/AGP pipeline into a second dex file the APK ships separately.

## Building and installing

Needs a real Android SDK location before anything else works — either set `ANDROID_HOME`, or create
`testapp/local.properties` (gitignored, machine-local, standard Android/Gradle convention - not
specific to this project) with `sdk.dir=<path to the Android SDK>`. Without one, configuration fails
outright with `SDK location not found` before any of the `konative*` properties below even matter.

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
  -PkonativeAapt2AarDir=<path to a directory of the real, unmodified .aar files for the same dependency set>
adb install -t -r <the real APK - see "Where the APK actually lands" below>
```

(A `gradle wrapper` run — or opening this folder in Android Studio once — is needed once to
generate `gradlew`/`gradle/wrapper/gradle-wrapper.jar`; neither is vendored in this skeleton. In the
meantime, any real, sufficiently-recent Gradle distribution invoked directly against this folder
works too — confirmed with a real, unmodified Gradle 9.4.1 against this project's pinned AGP 8.5.2 -
`gradlew`'s only real job is fetching+pinning one specific Gradle version automatically, it is not
otherwise special.)

**`konativeNdkPath` is needed even for a bare `gradle clean`, not just `assembleDebug`** - Gradle
evaluates this project's full configuration (including `defaultConfig.ndkVersion`) during its
configure phase regardless of which task is requested, so `clean` alone still hits AGP's
SDK-managed-NDK license/read-only-SDK wall (see the note on `konativeNdkPath` further down) unless
the override is passed to every invocation, `clean` included.

**The `kotlinc`+Compose-compiler-plugin+`aapt2`+`r8` CMake pipeline that produces the embedded dex is
now automated** (`cmake/modules/KonativeEmbedKotlinDex.cmake`, `ARCHITECTURE.md` section 6.6) — the
seven `konative*`-prefixed properties above forward straight through to the matching CMake cache
variables (`KONATIVE_KOTLINC`/`KONATIVE_R8`/`KONATIVE_ANDROID_JAR`/`KONATIVE_KOTLIN_CLASSPATH_DIR`/
`KONATIVE_AAPT2`/`KONATIVE_JAVAC`/`KONATIVE_AAPT2_AAR_DIR`, same machine-local-override pattern as
`konativeNdkPath` below), which is what `src/platform/android/CMakeLists.txt` uses by default now.
Each can also be set via the matching env var (`KONATIVE_KOTLINC`, `KONATIVE_R8`,
`KONATIVE_ANDROID_JAR`, `KONATIVE_KOTLIN_CLASSPATH_DIR`, `KONATIVE_AAPT2`, `KONATIVE_JAVAC`,
`KONATIVE_AAPT2_AAR_DIR`) instead of a Gradle property. `KONATIVE_KOTLIN_CLASSPATH_DIR` must be a
directory of pre-resolved dependency jars (Compose runtime/ui/foundation, activity,
lifecycle-runtime/viewmodel, savedstate, kotlinx-coroutines-android — NOT kotlin-stdlib.jar, that's
sourced automatically from the kotlinc distribution itself) — see `embedded_kotlin/README.md` for
how this directory is currently produced; real Maven dependency resolution from CMake is still an
open problem. `KONATIVE_AAPT2_AAR_DIR` must be a directory of the real, unmodified `.aar` files
(not just their extracted `classes.jar` — those carry no `res/` content) for that same dependency
set, used to generate real AAPT2-linked resource classes (`embedded_kotlin/README.md`'s
2026-07-18 update) — real AARs are already cached locally as a side effect of whatever produced
`KONATIVE_KOTLIN_CLASSPATH_DIR`.

`-PkonativeEmbeddedDexPath=<path to a real classes.dex>` (or the `KONATIVE_EMBEDDED_DEX_PATH` env
var) remains available as a manual override — set it to skip the automated pipeline entirely and
embed a hand-built or pre-built dex instead (containing a
`com.konative.generated.KonativeEntryPoint` class with a `@JvmStatic install(Application,
ByteBuffer?)` static method — the second parameter is the embedded resources.arsc blob, `null` is a
valid value if you don't have one — which `src/platform/android/jni_onload.cpp` calls). Useful on a
machine without kotlinc/r8/aapt2 installed at all. Optionally pair it with
`-PkonativeEmbeddedResourcesArscPath=<path to a real resources.arsc>` (or
`KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH`) if you have one; if you don't,
`src/platform/android/CMakeLists.txt` embeds a real, empty placeholder in its place so the link
still succeeds — a verify-subagent reviewing commit `f231098` caught this branch omitting the
resources blob entirely, which broke the link outright (the NDK toolchain's default
`-Wl,--no-undefined`), since `jni_onload.cpp` references it unconditionally regardless of which
branch supplied the dex.

One more optional `-P`/env-var override exists for the same reason `CMakeUserPresets.json` exists
for the standalone `cmake --preset` flow (see `BUILDING.md`) — a machine-local escape hatch that
never gets committed as a hardcoded path (the seven `konativeKotlinc`/`konativeR8`/`konativeAndroidJar`/
`konativeKotlinClasspathDir`/`konativeAapt2`/`konativeJavac`/`konativeAapt2AarDir` properties above are
this same kind of escape hatch too, just documented in the main example since they're needed on every
machine, not just some):

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
adb -s <device-serial> install -t -r <the real APK - see "Where the APK actually lands" below>
adb -s <device-serial> shell am start -n com.konative.testapp/.MainActivity
adb -s <device-serial> logcat -s Konative:V AndroidRuntime:E ActivityManager:E   # confirm the .so loaded and the embedded module initialized
adb -s <device-serial> exec-out screencap -p > konative_test.png                 # confirm the Compose UI actually rendered
```

**Where the APK actually lands is Gradle-version-dependent, not a fixed path - don't hardcode
either location, find it for real each time** (`find app/build -iname "*.apk"`): with one AGP
8.5.2 + Gradle combination this AGP/Gradle version left only a redirect-pointer file at the classic
`app/build/outputs/apk/debug/` and the real APK sat at `app/build/intermediates/apk/debug/` instead;
with a real, unmodified Gradle 9.4.1 invoked directly (2026-07-18), the SAME project produced the
real, complete APK at the classic `app/build/outputs/apk/debug/app-debug.apk` location instead, with
`intermediates/apk/` not used the same way. Confirmed via direct inspection both times, not assumed -
this is a real behavior difference across Gradle versions/invocations for the identical project, not
a one-off fluke. `-t` is required either way: AGP marks debug builds `testOnly`
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

**Verified end to end via `./gradlew assembleDebug`** (2026-07-18, LDPlayer x86_64 emulator, predates
AAPT2 — scoped historically, not a current claim): with the fully automated pipeline as it existed
that day — no `-PkonativeEmbeddedDexPath` override, no hand-built dex — `./gradlew assembleDebug`
with only the *original four* toolchain properties (`kotlinc`+r8, no `aapt2`/`javac`/AAR dir yet)
compiled `embedded_kotlin/` at build time, installed the resulting APK, launched it, and confirmed
via logcat + screenshot that `JNI_OnLoad` ran, the embedded dex blob passed its SHA-256 self-check,
`KonativeResourceProvider`'s opportunistic upgrade succeeded, `KonativeEntryPoint.install(Application)`
executed, and the real Jetpack Compose UI rendered — the same green-`Box`-plus-white-"Konative"-text
output as the original hand-built milestone (`ARCHITECTURE.md` §6.6/6.7), with zero manual
dex-building steps.

**The current pipeline** (`kotlinc`+Compose-compiler-plugin+`aapt2`+r8, all seven toolchain
properties above) was separately verified end to end via a direct `cmake --build` (not `gradlew`)
plus a root-push deploy onto LDPlayer the same day AAPT2 landed — identical correct render, clean
logcat, no regression.

**Then, genuinely re-confirmed via a real `./gradlew assembleDebug` run with all seven properties
together, AND for the first time ever on real physical hardware** (2026-07-18, same day): a real,
unmodified Gradle 9.4.1 invoked directly against this project (AGP 8.5.2) with all seven `konative*`
properties produced one universal debug APK (`app/build/outputs/apk/debug/app-debug.apk`, 8.1MB)
containing BOTH `lib/arm64-v8a/libkonative_app_native.so` AND `lib/x86_64/libkonative_app_native.so`
- confirming `abiFilters += listOf("arm64-v8a", "x86_64")` really does produce a working universal
build, not just a configured intent. Installed and launched on BOTH devices from this single APK:
- **The physical phone (`R3GL10AHL7P`, Galaxy S24-class, arm64-v8a)** - **the first time this whole
  Compose/dex-embedding/AAPT2 pipeline has ever run on real (non-emulated) hardware**, not just
  LDPlayer. Clean logcat (`upgrade_to_resource_aware_loader: upgraded successfully`, no
  `NoSuchFieldError`/`NoClassDefFoundError`/crash anywhere in the buffer), correct Compose UI render
  (screenshotted - identical green `Box` + white "Konative" text + "Konative Test App" title, real
  Samsung One UI navigation bar visible confirming genuine physical hardware, not a simulator).
- **The LDPlayer x86_64 emulator**, same APK, same clean result - confirming one real Gradle build
  correctly serves both real device architectures Konative currently targets.
This closes the previously-open gap (a `./gradlew assembleDebug` run specifically exercising all
seven properties together, and any real physical-hardware run at all, had not been done before) with
zero regressions found - the Gradle-driven and standalone-CMake-driven paths do share the same
underlying CMake modules and now both have real, independent, on-device proof. The two real,
one-time-setup gotchas hit while getting this far (needing a real `local.properties`/`ANDROID_HOME`,
and `konativeNdkPath` being required even for a bare `clean`) are both folded into "Building and
installing" above - neither is specific to Gradle 9.4.1 itself, both are ordinary Android/Gradle
project setup requirements this project's own docs simply hadn't needed to state yet, since nobody
had run a real `gradlew` build against a truly clean `testapp/` checkout before this point.
