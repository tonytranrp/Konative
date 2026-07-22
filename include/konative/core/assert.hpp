#pragma once

#include <cstdlib>

#include "konative/core/log.hpp"

// KONATIVE_ASSERT: a project-wide assert that always logs through konative::core::log (and
// therefore, on Android, through spdlog's logcat sink) before aborting - a plain assert() writes
// only to stderr, which is not visible via `adb logcat` on a release device.
#define KONATIVE_ASSERT(condition, message)                                                       \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            ::konative::core::log_error("assert failed: {} ({})", message, #condition);            \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)
