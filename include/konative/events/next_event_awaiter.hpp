#pragma once

#include <coro/event.hpp>
#include <coro/task.hpp>

#include "konative/events/dispatcher.hpp"

// A real spike/prototype of ARCHITECTURE.md section 9's explicitly-flagged "genuinely unproven"
// item: an entt::dispatcher + libcoro "await the next event" pattern. Lets a C++20 coroutine
// `co_await` the next occurrence of a specific event type, instead of registering a
// sink().connect() callback - an alternative consumption style for the SAME Dispatcher this
// codebase already uses everywhere else, not a replacement for it.
//
// Deliberately minimal, matching this section's own "needs a real spike/prototype before
// committing further architecture on top of an assumption that it works" framing - single-consumer,
// single-threaded use only (construct and co_await from the same thread that fires the event, the
// same constraint events::Dispatcher itself already has - konative::scheduling::CrossThreadEventQueue
// is the real primitive for a worker-thread producer, unrelated to this one).
//
// DESKTOP-ONLY for now, real reason (not an oversight): building this for android-arm64 empirically
// found Android NDK r28's bundled libc++ has no working std::jthread/std::stop_token (confirmed via
// the __cpp_lib_jthread feature-test macro being absent under <version>, identically at both API 26
// and API 30 - not an API-level gate). coro::task/coro::event themselves don't use stop_token, but
// libcoro compiles as one static-library CMake target with a fixed source list, and
// condition_variable.cpp/scheduler.cpp/thread_pool.cpp need it unconditionally - so the whole
// library fails to build for Android regardless of which headers this file uses. See
// include/konative/events/CMakeLists.txt's comment (libcoro is only linked `if(NOT ANDROID)`) and
// ARCHITECTURE.md section 9 for the full writeup.
namespace konative::events {

template <typename Event>
class NextEventAwaiter {
public:
    // Connects to dispatcher for as long as this object lives - the caller owns keeping it alive
    // (typically for the lifetime of whatever coroutine/subsystem wants to await this event type).
    explicit NextEventAwaiter(Dispatcher& dispatcher) {
        dispatcher.sink<Event>().template connect<&NextEventAwaiter::on_event>(*this);
    }

    // Suspends the calling coroutine until Event next fires (strictly AFTER this call, not any
    // occurrence before it - ready_.reset() below discards any stale already-set state from a
    // previous, already-completed wait), then resumes with a copy of the fired event. Safe to call
    // repeatedly, in a loop, for a genuinely repeating "await the next one, then the next one..."
    // consumption pattern - the real reason a one-shot coro::event alone isn't enough here.
    coro::task<Event> next() {
        ready_.reset();
        co_await ready_;
        co_return last_value_;
    }

private:
    void on_event(const Event& event) {
        last_value_ = event;
        ready_.set();
    }

    coro::event ready_;
    Event last_value_{};
};

} // namespace konative::events
