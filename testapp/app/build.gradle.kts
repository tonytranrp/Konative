// See testapp/README.md before editing this file. externalNativeBuild points at the REPO ROOT
// CMakeLists.txt (not a copy) - this Gradle module never owns its own C++/CMake sources, it only
// drives the same build ARCHITECTURE.md section 7 already documents, via Gradle's own CMake invocation
// instead of a standalone `cmake --preset`.
//
// The Kotlin Android plugin is here ONLY for MainActivity.kt (a single System.loadLibrary() call,
// see testapp/README.md) - it is NOT what dexes/embeds the real Compose UI code that ships inside
// the .so itself; that's a separate hand-rolled kotlinc+d8 CMake pipeline (see
// ARCHITECTURE.md/project_konative_autonomous_loop memory for the in-progress rework of that
// pipeline away from the earlier Kotlin/Native-AOT approach).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android") version "2.0.21"
}

android {
    namespace = "com.konative.testapp"
    compileSdk = 36
    // Pinned to the version actually installed under the SDK's build-tools/ - without this, AGP
    // defaults to whatever version it considers "tested" for this AGP release (34 for AGP 8.5.2)
    // and tries to auto-provision THAT one instead of reusing what's already there, hitting the
    // same license/read-only-SDK wall as the NDK override above solves.
    buildToolsVersion = "36.0.0"
    ndkVersion = "28.0.13004108"
    // ndkVersion above is the portable declaration (matches CMakePresets.json's own android-* presets).
    // AGP's own SDK-managed NDK provisioning needs a one-time interactive `sdkmanager --licenses`
    // AND write access to the SDK install dir - on this machine the SDK lives under
    // "Program Files (x86)" (admin-only, not writable by this build), so AGP's auto-provisioning
    // path can't work here even after licenses are accepted elsewhere. Same escape hatch shape as
    // konativeEmbeddedDexPath below: an optional local/uncommitted override, not a committed path.
    //   ./gradlew assembleDebug -PkonativeNdkPath=<path to an already-installed NDK>
    val ndkPathOverride = (project.findProperty("konativeNdkPath") as String?)
        ?: System.getenv("KONATIVE_NDK_PATH")
    if (ndkPathOverride != null) {
        ndkPath = ndkPathOverride
    }

    defaultConfig {
        applicationId = "com.konative.testapp"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        // Real device connected during development is arm64-v8a (Galaxy S24-class, API 36) -
        // x86_64 kept for emulator iteration, matching CMakePresets.json's own two Android presets.
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                // Disable everything the root CMakeLists.txt would otherwise build as a
                // top-level project (examples/tests are for the desktop-debug preset, not this
                // APK).
                //
                // PENDING REWORK (see project_konative_autonomous_loop memory / ARCHITECTURE.md):
                // KONATIVE_BUILD_KOTLIN_NATIVE currently controls the earlier, now-superseded
                // Kotlin/Native-AOT-compiles-to-EGL/GLES module (native/CMakeLists.txt). The
                // rendering direction reversed to JVM-hosted Jetpack Compose, dex-embedded and
                // loaded via InMemoryDexClassLoader - that pipeline's own CMake flag/module is
                // being built out in a follow-up commit once the open Compose/self-checking
                // research lands. Left OFF here deliberately until that lands, so this Gradle
                // build doesn't silently try to compile the superseded path.
                val baseArgs = mutableListOf(
                    "-DKONATIVE_BUILD_EXAMPLES=OFF",
                    "-DKONATIVE_BUILD_TESTS=OFF",
                    "-DKONATIVE_BUILD_KOTLIN_NATIVE=OFF"
                )
                // src/platform/android/CMakeLists.txt hard-fails with a clear FATAL_ERROR if this
                // isn't set (the automated kotlinc+Compose+d8 pipeline, ARCHITECTURE.md section 6.6,
                // doesn't exist yet) - forwarded here, not re-validated, so there is exactly one
                // place (CMake) that decides what counts as a valid path. Pass with:
                //   ./gradlew assembleDebug -PkonativeEmbeddedDexPath=<path to a real classes.dex>
                val dexPath = (project.findProperty("konativeEmbeddedDexPath") as String?)
                    ?: System.getenv("KONATIVE_EMBEDDED_DEX_PATH")
                if (dexPath != null) {
                    baseArgs += "-DKONATIVE_EMBEDDED_DEX_PATH=$dexPath"
                }
                // BUILDING.md's documented git.cmd-shim workaround (CPM/FetchContent's internal
                // `git rev-parse "HEAD^0"` breaks under an npm-installed git.cmd on Windows) -
                // a bare `-DGIT_EXECUTABLE=...` on the `gradle` command line does NOT reach this
                // CMake invocation (Gradle's own `-D` sets a JVM system property on the Gradle
                // daemon, it is not forwarded to the external native build process) - must go
                // through this same arguments() list like every other CMake define here.
                val gitExecutable = (project.findProperty("konativeGitExecutable") as String?)
                    ?: System.getenv("KONATIVE_GIT_EXECUTABLE")
                if (gitExecutable != null) {
                    baseArgs += "-DGIT_EXECUTABLE=$gitExecutable"
                }
                arguments(*baseArgs.toTypedArray())
                targets("konative_app_native")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.23.0+"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}
