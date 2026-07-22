#include <coro/task.hpp>
#include <doctest/doctest.h>

#include "konative/events/dispatcher.hpp"
#include "konative/events/next_event_awaiter.hpp"
#include "konative/events/next_event_awaiter_self_check.hpp"

namespace {

// Test-local event carrying real data - proves next() resumes with an actual copy of the fired
// event, not just a bare "it happened" signal (AppResumedEvent, used by the self-check below, is an
// empty struct and can't prove this on its own).
struct PayloadEvent {
    int value = 0;
};

} // namespace

TEST_CASE("run_next_event_awaiter_self_check: repeated await correctly waits for a fresh occurrence each time") {
    CHECK(konative::events::run_next_event_awaiter_self_check());
}

TEST_CASE("NextEventAwaiter: next() resumes with a copy of the actual fired event's data") {
    konative::events::Dispatcher dispatcher;
    konative::events::NextEventAwaiter<PayloadEvent> awaiter(dispatcher);

    bool completed = false;
    int received_value = 0;
    auto consumer = [&]() -> coro::task<void> {
        PayloadEvent event = co_await awaiter.next();
        received_value = event.value;
        completed = true;
        co_return;
    }();

    consumer.resume(); // runs to the real suspension point inside next() - ready_ isn't set yet
    CHECK_FALSE(consumer.is_ready());
    CHECK_FALSE(completed);

    dispatcher.trigger(PayloadEvent{42}); // synchronously resumes+completes the coroutine via ready_.set()

    CHECK(consumer.is_ready());
    CHECK(completed);
    CHECK(received_value == 42);
}
