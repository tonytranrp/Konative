#include <doctest/doctest.h>

#include "konative/events/dispatcher.hpp"
#include "konative/events/lifecycle/AppStartedEvent.hpp"
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
