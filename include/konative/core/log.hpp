#pragma once

#include <memory>

#include "konative/core/detail/platform.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
// KONATIVE_PLATFORM_ANDROID must come from the detail/platform.hpp include ABOVE, not below -
// an earlier draft of this file included platform.hpp last, so this #if always saw the macro as
// undefined (evaluates to 0 regardless of platform) and silently never included the Android sink
// header on Android at all: caught by an actual Android compile, not by desktop-debug (which
// exercises only the #else branch either way).
#if KONATIVE_PLATFORM_ANDROID
#include <spdlog/sinks/android_sink.h>
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

// Real spdlog-backed logging - on Android, routes to logcat via spdlog::android_logger_mt() (tag
// "Konative", matching every `adb logcat -s Konative:V` filter already used throughout this repo's
// docs/testapp) instead of raw stdout/stderr, which is NOT connected to logcat for a normal app
// process (a real, well-known NDK gotcha). A verification pass on commit 3618fb5 caught this: the
// embedded-dex SHA-256 self-check's own failure path (jni_onload.cpp) was silently invisible
// on-device because of it - directly contradicting this project's own "the self-check should
// report clearly, not silently swallow a failure" design intent (ARCHITECTURE.md section 6.5).
namespace konative::core {

namespace detail {

inline std::shared_ptr<spdlog::logger> make_default_logger() {
#if KONATIVE_PLATFORM_ANDROID
    return spdlog::android_logger_mt("konative", "Konative");
#else
    return spdlog::stdout_color_mt("konative");
#endif
}

// Function-local static, not a namespace-scope global - defers construction to first use so this
// works correctly regardless of static-initialization-order across translation units.
inline spdlog::logger& default_logger() {
    static const std::shared_ptr<spdlog::logger> logger = make_default_logger();
    return *logger;
}

} // namespace detail

template <typename... Args>
void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
    detail::default_logger().info(fmt_str, std::forward<Args>(args)...);
}

template <typename... Args>
void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
    detail::default_logger().error(fmt_str, std::forward<Args>(args)...);
}

} // namespace konative::core
