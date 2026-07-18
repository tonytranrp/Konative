// See testapp/README.md before editing this file. externalNativeBuild points at the REPO ROOT
// CMakeLists.txt (not a copy) - this Gradle module never owns its own C++/CMake sources, it only
// drives the same build ARCHITECTURE.md \xc2\xa77 already documents, via Gradle's own CMake invocation
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
    ndkVersion = "28.0.13004108"

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
                arguments(
                    "-DKONATIVE_BUILD_EXAMPLES=OFF",
                    "-DKONATIVE_BUILD_TESTS=OFF",
                    "-DKONATIVE_BUILD_KOTLIN_NATIVE=OFF"
                )
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
