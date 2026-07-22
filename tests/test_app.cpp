#include <doctest/doctest.h>

#include "konative/app/application.hpp"
#include "konative/events/lifecycle/AppDestroyedEvent.hpp"
#include "konative/events/lifecycle/AppPausedEvent.hpp"
#include "konative/events/lifecycle/AppResumedEvent.hpp"
#include "konative/events/lifecycle/AppStartedEvent.hpp"

namespace {

// Mirrors src/platform/android/jni_onload.cpp's real KonativeAndroidApp - the one place this
// mechanism previously had zero test coverage even though Application itself has existed since the
// project's first commit. Tracks which virtual hooks actually fired, not just whether the World's
// dispatcher saw the right event type - both halves of Application::start()/pause()/resume()/
// destroy()'s contract (dispatch the platform-agnostic event, THEN call the subclass override).
class TrackingApp final : public konative::app::Application {
public:
    bool started = false;
    bool resumed = false;
    bool paused = false;
    bool destroyed = false;

    void on_started() override { started = true; }
    void on_resumed() override { resumed = true; }
    void on_paused() override { paused = true; }
    void on_destroyed() override { destroyed = true; }
};

} // namespace

TEST_CASE("Application::start() dispatches AppStartedEvent through its own World, then calls on_started()") {
    TrackingApp app;
    bool event_seen = false;
    app.world().events().sink<konative::events::lifecycle::AppStartedEvent>().connect<
        +[](bool& flag, const konative::events::lifecycle::AppStartedEvent&) { flag = true; }>(event_seen);

    app.start();

    CHECK(event_seen);
    CHECK(app.started);
}

TEST_CASE("Application::resume() dispatches AppResumedEvent, then calls on_resumed()") {
    TrackingApp app;
    bool event_seen = false;
    app.world().events().sink<konative::events::lifecycle::AppResumedEvent>().connect<
        +[](bool& flag, const konative::events::lifecycle::AppResumedEvent&) { flag = true; }>(event_seen);

    app.resume();

    CHECK(event_seen);
    CHECK(app.resumed);
}

TEST_CASE("Application::pause() dispatches AppPausedEvent, then calls on_paused()") {
    TrackingApp app;
    bool event_seen = false;
    app.world().events().sink<konative::events::lifecycle::AppPausedEvent>().connect<
        +[](bool& flag, const konative::events::lifecycle::AppPausedEvent&) { flag = true; }>(event_seen);

    app.pause();

    CHECK(event_seen);
    CHECK(app.paused);
}

TEST_CASE("Application::destroy() dispatches AppDestroyedEvent, then calls on_destroyed()") {
    TrackingApp app;
    bool event_seen = false;
    app.world().events().sink<konative::events::lifecycle::AppDestroyedEvent>().connect<
        +[](bool& flag, const konative::events::lifecycle::AppDestroyedEvent&) { flag = true; }>(event_seen);

    app.destroy();

    CHECK(event_seen);
    CHECK(app.destroyed);
}

TEST_CASE("A real Android session's callback order (start, resume, pause, resume, destroy) fires "
          "every hook exactly the times it should, on the SAME World instance") {
    // Mirrors src/platform/android/jni_onload.cpp's real usage: one Application instance living for
    // the whole process, receiving repeated resume/pause cycles as the real Activity backgrounds
    // and foregrounds, exactly like KonativeEntryPoint.kt's ActivityLifecycleCallbacks now drives it
    // (onActivityCreated -> STARTED once; onActivityResumed/onActivityPaused any number of times;
    // onActivityDestroyed -> DESTROYED once).
    TrackingApp app;
    int resumed_count = 0;
    int paused_count = 0;
    app.world().events().sink<konative::events::lifecycle::AppResumedEvent>().connect<
        +[](int& count, const konative::events::lifecycle::AppResumedEvent&) { ++count; }>(resumed_count);
    app.world().events().sink<konative::events::lifecycle::AppPausedEvent>().connect<
        +[](int& count, const konative::events::lifecycle::AppPausedEvent&) { ++count; }>(paused_count);

    app.start();
    app.resume();
    app.pause();
    app.resume();
    app.destroy();

    CHECK(resumed_count == 2);
    CHECK(paused_count == 1);
    CHECK(app.started);
    CHECK(app.destroyed);
}
