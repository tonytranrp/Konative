#pragma once

// Konative's equivalent of android-activity's `android_main(AndroidApp)` (ARCHITECTURE.md \xc2\xa76.1):
// a Konative application defines exactly one function with this signature, and the platform glue
// (src/platform/android/android_main.cpp) constructs/drives it. Kept in its own header (rather
// than folded into application.hpp) since it's the one true entry-point contract, not part of the
// Application base class itself.
namespace konative::app {

class Application;

// Implemented by the application author, exactly once per binary.
Application& create_application();

} // namespace konative::app
