#include <doctest/doctest.h>

#include "konative/events/dispatcher.hpp"
#include "konative/events/input/KeyEvent.hpp"
#include "konative/events/input/TouchDownEvent.hpp"
#include "konative/events/input/TouchMoveEvent.hpp"
#include "konative/events/input/TouchUpEvent.hpp"
#include "konative/events/lifecycle/AppStartedEvent.hpp"
#include "konative/events/persistence/SnapshotSavedEvent.hpp"
#include "konative/events/window/WindowFocusChangedEvent.hpp"
#include "konative/events/window/WindowResizedEvent.hpp"

TEST_CASE("dispatcher delivers a triggered event immediately") {
    konative::events::Dispatcher dispatcher;
    bool received = false;
    dispatcher.sink<konative::events::lifecycle::AppStartedEvent>().connect<
        +[](bool& flag, const konative::events::lifecycle::AppStartedEvent&) { flag = true; }>(received);
    dispatcher.trigger(konative::events::lifecycle::AppStartedEvent{});
    CHECK(received);
}

TEST_CASE("dispatcher defers an enqueued event until update()") {
    konative::events::Dispatcher dispatcher;
    int last_width = 0;
    dispatcher.sink<konative::events::window::WindowResizedEvent>().connect<
        +[](int& width, const konative::events::window::WindowResizedEvent& e) { width = e.width; }>(last_width);

    dispatcher.enqueue(konative::events::window::WindowResizedEvent{1920, 1080});
    CHECK(last_width == 0); // not delivered yet
    dispatcher.update();
    CHECK(last_width == 1920);
}

// The 6 cases below close a real gap: each of these event types is production-wired (a real
// on-device producer in KonativeEntryPoint.kt and a real consumer in jni_onload.cpp) but had zero
// exposure in the desktop test suite before this - only ever exercised via manual on-device
// testing, so a regression in any of these could land silently through desktop CI.

TEST_CASE("TouchDownEvent carries pointer id and coordinates through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    konative::events::input::TouchDownEvent received{};
    dispatcher.sink<konative::events::input::TouchDownEvent>().connect<
        +[](konative::events::input::TouchDownEvent& out, const konative::events::input::TouchDownEvent& e) { out = e; }>(received);
    dispatcher.trigger(konative::events::input::TouchDownEvent{7, 12.5F, 34.0F});
    CHECK(received.pointer_id == 7);
    CHECK(received.x == doctest::Approx(12.5F));
    CHECK(received.y == doctest::Approx(34.0F));
}

TEST_CASE("TouchMoveEvent carries pointer id and coordinates through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    konative::events::input::TouchMoveEvent received{};
    dispatcher.sink<konative::events::input::TouchMoveEvent>().connect<
        +[](konative::events::input::TouchMoveEvent& out, const konative::events::input::TouchMoveEvent& e) { out = e; }>(received);
    dispatcher.trigger(konative::events::input::TouchMoveEvent{3, 100.0F, 200.0F});
    CHECK(received.pointer_id == 3);
    CHECK(received.x == doctest::Approx(100.0F));
    CHECK(received.y == doctest::Approx(200.0F));
}

TEST_CASE("TouchUpEvent carries pointer id and coordinates through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    konative::events::input::TouchUpEvent received{};
    dispatcher.sink<konative::events::input::TouchUpEvent>().connect<
        +[](konative::events::input::TouchUpEvent& out, const konative::events::input::TouchUpEvent& e) { out = e; }>(received);
    dispatcher.trigger(konative::events::input::TouchUpEvent{9, 1.0F, 2.0F});
    CHECK(received.pointer_id == 9);
    CHECK(received.x == doctest::Approx(1.0F));
    CHECK(received.y == doctest::Approx(2.0F));
}

TEST_CASE("KeyEvent carries key code and down/up state through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    konative::events::input::KeyEvent received{};
    dispatcher.sink<konative::events::input::KeyEvent>().connect<
        +[](konative::events::input::KeyEvent& out, const konative::events::input::KeyEvent& e) { out = e; }>(received);

    dispatcher.trigger(konative::events::input::KeyEvent{24, true}); // KEYCODE_VOLUME_UP-shaped, down
    CHECK(received.key_code == 24);
    CHECK(received.is_down);

    dispatcher.trigger(konative::events::input::KeyEvent{24, false}); // same key, up
    CHECK_FALSE(received.is_down);
}

TEST_CASE("WindowFocusChangedEvent carries focus state through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    bool has_focus = true;
    dispatcher.sink<konative::events::window::WindowFocusChangedEvent>().connect<
        +[](bool& out, const konative::events::window::WindowFocusChangedEvent& e) { out = e.has_focus; }>(has_focus);

    dispatcher.trigger(konative::events::window::WindowFocusChangedEvent{false});
    CHECK_FALSE(has_focus);
    dispatcher.trigger(konative::events::window::WindowFocusChangedEvent{true});
    CHECK(has_focus);
}

TEST_CASE("SnapshotSavedEvent carries the real serialized byte size and persistence outcome through the dispatcher") {
    konative::events::Dispatcher dispatcher;
    konative::events::persistence::SnapshotSavedEvent last{};
    dispatcher.sink<konative::events::persistence::SnapshotSavedEvent>().connect<
        +[](konative::events::persistence::SnapshotSavedEvent& out,
            const konative::events::persistence::SnapshotSavedEvent& e) { out = e; }>(last);

    dispatcher.trigger(konative::events::persistence::SnapshotSavedEvent{145516, true});
    CHECK(last.byte_size == 145516);
    CHECK(last.persisted_to_disk);

    // The single-field shape older producers used still aggregate-initializes - persisted_to_disk
    // defaults false (the honest default: nothing proved a durable write happened).
    dispatcher.trigger(konative::events::persistence::SnapshotSavedEvent{42});
    CHECK(last.byte_size == 42);
    CHECK_FALSE(last.persisted_to_disk);
}
