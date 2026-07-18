#pragma once

#include <fmt/format.h>

// Deliberately a thin fmt-based wrapper for the skeleton, not yet wired to spdlog's Android
// logcat sink - see ARCHITECTURE.md section 4/section 8. Fill in the spdlog::android_logger_mt() sink once the
// platform/android module is built out.
namespace konative::core {

template <typename... Args>
void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
    fmt::print("[konative][info] {}\n", fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename... Args>
void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
    fmt::print(stderr, "[konative][error] {}\n", fmt::format(fmt_str, std::forward<Args>(args)...));
}

} // namespace konative::core
