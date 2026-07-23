#pragma once

#include <memory>

#include "konative/core/detail/platform.hpp"

#include <fmt/format.h>

// spdlog v1.17.0 (the current latest release - confirmed via a real `git ls-remote --tags`, no
// newer tag exists yet to fix this upstream) internally converts its own fmt::format_string<Args...>
// argument through spdlog::string_view_t on its way into logger::log() - under
// SPDLOG_FMT_EXTERNAL_HO (this project's real, deliberate mode, KonativeDependencies.cmake's own
// comment), spdlog::string_view_t IS fmt::basic_string_view<char>, so this is really
// fmt::format_string<Args...>'s own implicit conversion operator to basic_string_view<char> - which
// fmt 12.2.0 has since marked [[deprecated]] out from under spdlog's still-current release. A real,
// external version-skew between two independently-pinned dependencies, not a call-site issue in
// this file's own log_info()/log_error() (both already take the modern, correct
// fmt::format_string<Args...>). Wrapping the #include here, not log_info()/log_error()'s own
// definitions below, because the warning is diagnosed at its canonical location INSIDE spdlog's own
// logger.h template body - confirmed empirically that a pragma around this file's own call sites
// does not reach it, regardless of whether the call is direct or through the KONATIVE_ASSERT macro;
// suppressing at the #include itself is the standard, reliable pattern for a warning whose root
// cause lives entirely inside a vendored header's own template code. Scoped to exactly this warning
// (not a project-wide -Wno-deprecated-declarations, which would hide a genuinely new, actionable
// future deprecation just as easily as this one).
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

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

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
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

// Constructs a spdlog::logger DIRECTLY from a sink, deliberately NOT via the
// spdlog::android_logger_mt()/stdout_color_mt() convenience functions - those register the new
// logger into spdlog's process-global name registry (spdlog::details::registry), which throws
// (spdlog::spdlog_ex, a std::exception) if a logger named "konative" is ever already registered.
// core/result.hpp's own doc comment and jni/README.md both state a C++ exception must never
// unwind across the JNI boundary this logger can first get constructed at (log_error's first-ever
// call can happen as early as JNI_OnLoad's own first line, jni_onload.cpp) - a verification pass
// on commit f42bd48 caught that the registry-throwing convenience functions left that boundary
// unprotected. Constructing the logger directly, with no registry involved, removes the throwing
// path entirely rather than adding a try/catch around it - the same "fix the real cause, don't
// patch around it" approach this project has taken for its other real bugs.
inline std::shared_ptr<spdlog::logger> make_default_logger() {
#if KONATIVE_PLATFORM_ANDROID
    auto sink = std::make_shared<spdlog::sinks::android_sink_mt>("Konative");
#else
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#endif
    return std::make_shared<spdlog::logger>("konative", std::move(sink));
}

// Function-local static, not a namespace-scope global - defers construction to first use so this
// works correctly regardless of static-initialization-order across translation units.
inline spdlog::logger& default_logger() {
    static const std::shared_ptr<spdlog::logger> logger = make_default_logger();
    return *logger;
}

} // namespace detail

// log_info()/log_error() already take the modern, correct fmt::format_string<Args...> - see the
// pragma-wrapped #include block above for what's actually deprecated and why (a real spdlog-v1.17.0
// -vs-fmt-12.2.0 version skew entirely inside spdlog's own vendored header, not a call-site issue
// here). No further suppression needed at this end - confirmed empirically that wrapping the
// #include is both necessary AND sufficient; a pragma around these two functions' own definitions
// (tried first) made no measurable difference, since the warning's canonical location is inside
// spdlog's own template body, not here.
template <typename... Args>
void log_info(fmt::format_string<Args...> fmt_str, Args&&... args) {
    detail::default_logger().info(fmt_str, std::forward<Args>(args)...);
}

template <typename... Args>
void log_error(fmt::format_string<Args...> fmt_str, Args&&... args) {
    detail::default_logger().error(fmt_str, std::forward<Args>(args)...);
}

} // namespace konative::core
