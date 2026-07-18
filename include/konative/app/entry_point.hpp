#pragma once

// A Konative application defines exactly one function with this signature; whatever platform glue
// is compiled in constructs/drives it. Historically driven by src/platform/android/android_main.cpp
// (ARCHITECTURE.md section 6.1, now deleted along with that file); not yet wired to anything in the
// current JNI_OnLoad-based design (section 6.4) - the ECS/app lifecycle's relationship to Activity
// lifecycle callbacks in the new design is a real open item, not yet decided. Kept in its own
// header (rather than folded into application.hpp) since it's the one true entry-point contract,
// not part of the Application base class itself.
namespace konative::app {

class Application;

// Implemented by the application author, exactly once per binary.
Application& create_application();

} // namespace konative::app
