#pragma once

// Wraps android_native_app_glue's `struct android_app` (or GameActivity's own replacement of it -
// ARCHITECTURE.md section 6.1's NativeActivity-vs-GameActivity choice is a build-time option, not
// hardcoded here). Deliberately just forward-declares the type this header's own consumers need
// to name - the real struct comes from whichever glue's own header the platform CMakeLists.txt
// links against.
struct android_app;

namespace konative::platform::android {

using NativeAppHandle = struct android_app*;

} // namespace konative::platform::android
