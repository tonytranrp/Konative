#pragma once

#include "konative/platform/android/native_app_glue.hpp"

// A thin wrapper around ALooper_pollOnce - kept separate from activity_bridge.hpp so the polling
// mechanics (timeout handling, LOOPER_ID_MAIN vs LOOPER_ID_INPUT vs LOOPER_ID_USER dispatch) can
// be unit-tested/iterated on independently of the top-level run loop.
namespace konative::platform::android {

// Returns false once APP_CMD_DESTROY has been observed and the app should stop polling.
bool pump_once(NativeAppHandle app_handle, int timeout_millis);

} // namespace konative::platform::android
