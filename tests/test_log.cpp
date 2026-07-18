#include <doctest/doctest.h>

#include "konative/core/log.hpp"

// Desktop-only coverage (the #else / non-Android sink branch) - there is no way to exercise the
// KONATIVE_PLATFORM_ANDROID branch from this desktop-debug preset at all. Real Android-side
// verification is on-device (project-konative-autonomous-loop memory, iteration 13's logcat proof).
// Primarily exists so make_default_logger()/log_info()/log_error() are actually compiled by
// SOMETHING in this preset - before this test existed, nothing in desktop-debug included log.hpp
// at all, so editing it produced zero compiler feedback (caught: an edit to this exact file was
// silently uncompiled, "ninja: no work to do", despite a real change).
TEST_CASE("log_info/log_error do not throw, and can be called repeatedly") {
    CHECK_NOTHROW(konative::core::log_info("test message {}", 42));
    CHECK_NOTHROW(konative::core::log_error("test error {}", "oops"));
    // A second call exercises the lazily-constructed function-local static logger on its
    // already-initialized path, not just first-construction.
    CHECK_NOTHROW(konative::core::log_info("second call"));
}
