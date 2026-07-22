#pragma once

#include <coro/task.hpp>

#include "konative/events/dispatcher.hpp"
#include "konative/events/lifecycle/AppResumedEvent.hpp"
#include "konative/events/next_event_awaiter.hpp"

// A real, permanent self-check for NextEventAwaiter - same "code checks itself" precedent as
// checked_blob.hpp and scheduling/taskflow_self_check.hpp. Proves the actual mechanism (a coroutine
// co_await-ing a Dispatcher event, then repeating for a genuinely fresh next occurrence) works
// correctly, on whatever target this runs on - not just that it compiles.
namespace konative::events {

// Drives a real consumer coroutine through 3 repeated `co_await awaiter.next()` calls, firing
// AppResumedEvent between each one, and verifies: (1) the coroutine only ever completes each step
// AFTER the matching trigger, never before or without one; (2) it correctly awaits a FRESH
// occurrence each time (not a stale "already set" state left over from the previous one - the real
// reason NextEventAwaiter::next() resets its internal coro::event before awaiting, and the one
// detail most likely to have a real bug if this self-check is ever removed and someone changes
// that ordering); (3) all 3 steps complete, in order, by the time all 3 triggers have fired.
inline bool run_next_event_awaiter_self_check() {
    Dispatcher dispatcher;
    NextEventAwaiter<lifecycle::AppResumedEvent> awaiter(dispatcher);

    int completions = 0;
    auto consumer = [&]() -> coro::task<void> {
        co_await awaiter.next();
        ++completions;
        co_await awaiter.next();
        ++completions;
        co_await awaiter.next();
        ++completions;
        co_return;
    }();

    consumer.resume();
    if (completions != 0 || consumer.is_ready()) {
        return false; // must not complete before the first real trigger
    }

    dispatcher.trigger(lifecycle::AppResumedEvent{});
    if (completions != 1 || consumer.is_ready()) {
        return false; // must complete exactly step 1, then suspend again awaiting a fresh occurrence
    }

    dispatcher.trigger(lifecycle::AppResumedEvent{});
    if (completions != 2 || consumer.is_ready()) {
        return false;
    }

    dispatcher.trigger(lifecycle::AppResumedEvent{});
    if (completions != 3 || !consumer.is_ready()) {
        return false; // must complete step 3 and finish after exactly 3 real triggers, not more or fewer
    }

    return true;
}

} // namespace konative::events
