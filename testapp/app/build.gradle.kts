// See testapp/README.md before editing this file. externalNativeBuild points at the REPO ROOT
// CMakeLists.txt (not a copy) - this Gradle module never owns its own C++/CMake sources, it only
// drives the same build ARCHITECTURE.md section 7 already documents, via Gradle's own CMake invocation
// instead of a standalone `cmake --preset`.
//
// The Kotlin Android plugin is here ONLY for MainActivity.kt (a single System.loadLibrary() call,
// see testapp/README.md) - it is NOT what dexes/embeds the real Compose UI code that ships inside
// the .so itself; that's a separate, automated kotlinc+aapt2+r8 CMake pipeline (see
// ARCHITECTURE.md section 6.6 and embedded_kotlin/README.md).
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
                // KONATIVE_BUILD_KOTLIN_NATIVE controls the earlier, now-superseded (confirmed
                // landed, not pending - ARCHITECTURE.md SS6.7) Kotlin/Native-AOT-compiles-to-EGL/GLES
                // module (native/CMakeLists.txt). Rendering is JVM-hosted Jetpack Compose,
                // dex-embedded and loaded via InMemoryDexClassLoader - that pipeline's own CMake
                // module (KonativeEmbedKotlinDex.cmake) has been built and is what this Gradle build
                // actually drives by default now (see konativeEmbeddedDexPath below). Left OFF here
                // permanently, not provisionally, so this Gradle build never tries to compile the
                // superseded path.
                val baseArgs = mutableListOf(
                    "-DKONATIVE_BUILD_EXAMPLES=OFF",
                    "-DKONATIVE_BUILD_TESTS=OFF",
                    "-DKONATIVE_BUILD_KOTLIN_NATIVE=OFF"
                )
                // Optional manual override - src/platform/android/CMakeLists.txt falls back to the
                // automated konative_embed_kotlin_dex() pipeline (kotlinc+r8, see
                // cmake/modules/KonativeEmbedKotlinDex.cmake) when this isn't set, so it's no longer
                // required the way it was before that pipeline existed. Still useful for a hand-built
                // dex, or a machine without kotlinc/r8 installed. Pass with:
                //   ./gradlew assembleDebug -PkonativeEmbeddedDexPath=<path to a real classes.dex>
                val dexPath = (project.findProperty("konativeEmbeddedDexPath") as String?)
                    ?: System.getenv("KONATIVE_EMBEDDED_DEX_PATH")
                if (dexPath != null) {
                    baseArgs += "-DKONATIVE_EMBEDDED_DEX_PATH=$dexPath"
                }
                // Optional companion to konativeEmbeddedDexPath above - pair it with a real
                // resources.arsc if you have one. If you don't, src/platform/android/CMakeLists.txt's
                // own KONATIVE_EMBEDDED_DEX_PATH branch embeds a real, empty placeholder instead (a
                // null/absent resources buffer is a valid, documented configuration - see
                // testapp/README.md), so this is genuinely optional, not required to keep the link
                // working.
                val resourcesArscPath = (project.findProperty("konativeEmbeddedResourcesArscPath") as String?)
                    ?: System.getenv("KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH")
                if (resourcesArscPath != null) {
                    baseArgs += "-DKONATIVE_EMBEDDED_RESOURCES_ARSC_PATH=$resourcesArscPath"
                }
                // The automated pipeline's own machine-local toolchain paths - same forwarding shape
                // as konativeNdkPath/konativeGitExecutable above, and the same variables
                // CMakeUserPresets.json sets for a plain `cmake --preset` build (see that file and
                // KonativeEmbedKotlinDex.cmake's own top comment). Only needed when konativeEmbeddedDexPath
                // is NOT set - harmless to pass either way, since CMake ignores unused -D values with
                // just a warning.
                val kotlinc = (project.findProperty("konativeKotlinc") as String?)
                    ?: System.getenv("KONATIVE_KOTLINC")
                if (kotlinc != null) {
                    baseArgs += "-DKONATIVE_KOTLINC=$kotlinc"
                }
                val r8 = (project.findProperty("konativeR8") as String?)
                    ?: System.getenv("KONATIVE_R8")
                if (r8 != null) {
                    baseArgs += "-DKONATIVE_R8=$r8"
                }
                val androidJar = (project.findProperty("konativeAndroidJar") as String?)
                    ?: System.getenv("KONATIVE_ANDROID_JAR")
                if (androidJar != null) {
                    baseArgs += "-DKONATIVE_ANDROID_JAR=$androidJar"
                }
                val kotlinClasspathDir = (project.findProperty("konativeKotlinClasspathDir") as String?)
                    ?: System.getenv("KONATIVE_KOTLIN_CLASSPATH_DIR")
                if (kotlinClasspathDir != null) {
                    baseArgs += "-DKONATIVE_KOTLIN_CLASSPATH_DIR=$kotlinClasspathDir"
                }
                // aapt2/javac - real resource-ID linking (KonativeCompileKotlinDex.cmake's Step 1.5),
                // replacing embedded_kotlin/r_shim/'s old hand-shimmed placeholder R classes.
                val aapt2 = (project.findProperty("konativeAapt2") as String?)
                    ?: System.getenv("KONATIVE_AAPT2")
                if (aapt2 != null) {
                    baseArgs += "-DKONATIVE_AAPT2=$aapt2"
                }
                val javac = (project.findProperty("konativeJavac") as String?)
                    ?: System.getenv("KONATIVE_JAVAC")
                if (javac != null) {
                    baseArgs += "-DKONATIVE_JAVAC=$javac"
                }
                val aapt2AarDir = (project.findProperty("konativeAapt2AarDir") as String?)
                    ?: System.getenv("KONATIVE_AAPT2_AAR_DIR")
                if (aapt2AarDir != null) {
                    baseArgs += "-DKONATIVE_AAPT2_AAR_DIR=$aapt2AarDir"
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
