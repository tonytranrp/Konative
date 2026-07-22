#pragma once

// A Konative application defines exactly one function with this signature; whatever platform glue
// is compiled in constructs/drives it. Historically driven by src/platform/android/android_main.cpp
// (ARCHITECTURE.md section 6.1, now deleted along with that file); real and wired in the current
// JNI_OnLoad-based design (section 6.4) since 2026-07-21 -
// src/platform/android/jni_onload.cpp's KonativeAndroidApp is the one real create_application()
// implementation for that target, and its own on_started()/on_tick()/etc. overrides are driven by
// KonativeEntryPoint.kt's ActivityLifecycleCallbacks + Choreographer.FrameCallback calling back into
// native code via RegisterNatives - see that file for the full, real wiring. Kept in its own header
// (rather than folded into application.hpp) since it's the one true entry-point contract, not part
// of the Application base class itself.
namespace konative::app {

class Application;

// Implemented by the application author, exactly once per binary.
Application& create_application();

} // namespace konative::app
