#pragma once

#include "konative/platform/android/native_app_glue.hpp"

// The one true native entry point on Android - src/platform/android/android_main.cpp implements
// this, constructs konative::app::create_application(), and drives the ALooper pump
// (ARCHITECTURE.md section 6.1). Declared here so it has one canonical signature shared between the
// glue's own expected `void android_main(struct android_app*)` C entry point and this file.
namespace konative::platform::android {

void run_application(NativeAppHandle app_handle);

} // namespace konative::platform::android
