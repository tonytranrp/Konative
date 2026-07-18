package com.konative.testapp

import android.app.Activity

/**
 * The ONLY Kotlin file in this project (testapp/README.md) - its entire job is loading the
 * native .so. Every other concern (rendering via the embedded, dex-loaded Compose UI - see
 * ARCHITECTURE.md's rendering direction - ECS, events) lives inside konative_app_native's own
 * native code, reached back into the JVM only through the InMemoryDexClassLoader-loaded module
 * that .so embeds. Do not add fields, methods, or logic here - this mirrors GameHub's own
 * testapp/MainActivity.java exactly, on purpose.
 */
class MainActivity : Activity() {
    companion object {
        init {
            System.loadLibrary("konative_app_native")
        }
    }
}
