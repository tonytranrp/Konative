// Load-bearing (ARCHITECTURE.md section 2): wraps the real android_native_app_glue ALooper_pollOnce
// call, which android_native_app_glue.h's own `struct android_poll_source::process` contract
// requires a real .c/.cpp translation unit to drive - see include/konative/platform/android/
// looper_pump.hpp for why this is kept separate from activity_bridge.cpp.

#include "konative/platform/android/looper_pump.hpp"

#include <android/looper.h>
#include <android_native_app_glue.h>

namespace konative::platform::android {

bool pump_once(NativeAppHandle app_handle, int timeout_millis) {
    int out_fd = 0;
    int out_events = 0;
    void* out_data = nullptr;
    const int ident = ALooper_pollOnce(timeout_millis, &out_fd, &out_events, &out_data);

    if ((ident == LOOPER_ID_MAIN || ident == LOOPER_ID_INPUT) && out_data != nullptr) {
        auto* source = static_cast<android_poll_source*>(out_data);
        source->process(app_handle, source);
    }

    return app_handle->destroyRequested == 0;
}

} // namespace konative::platform::android
