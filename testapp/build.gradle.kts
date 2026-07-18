// Root Gradle project for the testapp - see testapp/README.md. This project's ONLY job is
// packaging the .so the sibling root CMakeLists.txt already builds; it must never grow real
// application logic (that all lives in include/konative/**.hpp and native/src/**.kt).
plugins {
    id("com.android.application") version "8.5.2" apply false
}
