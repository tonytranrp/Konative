#pragma once

#include "konative/app/application.hpp"

// Translates the glue layer's raw APP_CMD_* integers into calls on konative::app::Application -
// kept as a single, small, honest switch rather than spread across the platform code, since this
// is the ONE place the "which command means what" knowledge should live (ARCHITECTURE.md section 6.1).
namespace konative::platform::android::detail {

// Mirrors the android_native_app_glue.h / GameActivity APP_CMD_* enum values - kept as a local
// copy rather than including the NDK/GameActivity header from a public Konative header, so this
// header stays includable from a non-Android build for unit testing the switch logic itself.
enum class AppCmd : int {
    InputChanged = 0,
    InitWindow = 1,
    TermWindow = 2,
    WindowResized = 3,
    WindowRedrawNeeded = 4,
    ContentRectChanged = 5,
    GainedFocus = 6,
    LostFocus = 7,
    ConfigChanged = 8,
    LowMemory = 9,
    Start = 10,
    Resume = 11,
    SaveState = 12,
    Pause = 13,
    Stop = 14,
    Destroy = 15,
};

inline void handle_app_cmd(konative::app::Application& app, AppCmd cmd) {
    switch (cmd) {
        case AppCmd::Start: app.start(); break;
        case AppCmd::Resume: app.resume(); break;
        case AppCmd::Pause: app.pause(); break;
        case AppCmd::Destroy: app.destroy(); break;
        default: break; // InitWindow/TermWindow/etc. are handled by konative::render, not here.
    }
}

} // namespace konative::platform::android::detail
