// The native entry point android_native_app_glue's own glue thread calls once the process has
// bootstrapped (ARCHITECTURE.md \xc2\xa76.1). This is intentionally the ONLY genuinely load-bearing
// translation unit for the whole Android app target - everything it calls into lives in
// include/konative/**.hpp.

#include "konative/app/entry_point.hpp"
#include "konative/platform/android/activity_bridge.hpp"
#include "konative/platform/android/native_app_glue.hpp"

extern "C" void android_main(struct android_app* app_handle) {
    konative::platform::android::run_application(app_handle);
}
