// The real native entry point for the current design (ARCHITECTURE.md section 6.4) - replaces the
// deleted android_main.cpp/activity_bridge.cpp/looper_pump.cpp (android_native_app_glue/
// NativeActivity, section 6.1, superseded). System.loadLibrary() in testapp/'s MainActivity.kt
// triggers this automatically; there is no other native entry point for the app target anymore.

#include <cstddef>
#include <cstdint>
#include <span>

#include <jni.h>

#include "konative/app/application.hpp"
#include "konative/app/entry_point.hpp"
#include "konative/core/log.hpp"
#include "konative/embed/checked_blob.hpp"
#include "konative/events/input/TouchDownEvent.hpp"
#include "konative/events/input/TouchMoveEvent.hpp"
#include "konative/events/input/TouchUpEvent.hpp"
#include "konative/jni/dex_loader.hpp"
#include "konative/scheduling/taskflow_self_check.hpp"

extern "C" {
extern const unsigned char konative_app_dex_start[];
extern const unsigned char konative_app_dex_end[];
extern const uint64_t konative_app_dex_size;
extern const unsigned char konative_app_dex_expected_sha256[32];

// Sibling blob, same .incbin mechanism (KonativeEmbedBlob.cmake), the real resources.arsc
// KonativeCompileKotlinDex.cmake's Step 1.5 now keeps instead of discarding - feeds the general,
// API-30+ ResourcesLoader runtime mechanism (embedded_kotlin/KonativeResourcesLoader.kt). Unlike the
// dex blob's self-check, a failure here is NON-fatal to the whole load (see below) - it only means
// that one enhancement doesn't activate, not that the app itself can't start.
extern const unsigned char konative_app_dex_resources_start[];
extern const unsigned char konative_app_dex_resources_end[];
extern const uint64_t konative_app_dex_resources_size;
extern const unsigned char konative_app_dex_resources_expected_sha256[32];
}

namespace {

constexpr const char* kEntryPointClass = "com.konative.generated.KonativeEntryPoint";

// A minimal, real ECS component - proving `Registry::create()`/`emplace<T>()` and a real System
// (registered the standard way, via `World::systems().add()`) actually run in the real, shipping
// app, not just in desktop unit tests. Everything else this session wired up (Dispatcher/events via
// lifecycle dispatch, a real per-frame tick driver, Taskflow) proves the rest of the ECS core works
// here - nothing yet had exercised Registry/View/System together in this app until now.
struct HeartbeatCounter {
    std::uint64_t ticks = 0;
};

// Registered via `world().systems().add()` in `on_started()` below - `SystemSequence::run()` calls
// every registered system, in registration order, once per real `World::tick()` call (itself driven
// by Choreographer via `native_dispatch_tick`, see below). `registry.view<T>().each()` is EnTT's
// own standard structured-binding iteration idiom (confirmed against the real vendored
// `entt.hpp`'s `each()`/`extended_storage_iterator`, not assumed) - not a Konative invention.
void increment_heartbeat_counters(konative::ecs::Registry& registry, float /*delta_seconds*/) {
    for (auto [entity, counter] : registry.view<HeartbeatCounter>().each()) {
        ++counter.ticks;
    }
}

// Resolves the "real open item, not yet decided" flagged by both include/konative/app/entry_point
// .hpp and include/konative/app/detail/lifecycle_bridge.hpp's own doc comments: the C++ ECS/events
// core (World/Registry/Dispatcher) has been buildable since the very first commit but was never
// actually instantiated anywhere in the real, shipping app - only desktop tests/examples ever ran
// it. KonativeEntryPoint.kt's own ActivityLifecycleCallbacks (already registered for Jetpack's
// Lifecycle system, for Compose's own sake) now ALSO calls back into native code on each of the 4
// transitions konative::app::Application models (see the RegisterNatives call below and
// KonativeEntryPoint.kt's nativeDispatchLifecycle) - this class is the one real
// konative::app::create_application() implementation for this target, "implemented by the
// application author, exactly once per binary" per entry_point.hpp's own contract.
//
// Deliberately trivial (matches examples/minimal_triangle's own "prove the plumbing, not a real
// app" scope, ARCHITECTURE.md section 2) - logs on each transition so this is independently
// verifiable via logcat alone, the same self-checking ethos as every other mechanism in this
// codebase. World::tick() is now driven too, via Android's Choreographer (see
// KonativeEntryPoint.kt's own frame-callback loop and native_dispatch_tick below) - Compose
// (JVM-hosted) still owns real rendering/frame scheduling; this is a SEPARATE, additive per-frame
// heartbeat for the C++ ECS/systems side, not a rendering driver.
class KonativeAndroidApp final : public konative::app::Application {
public:
    void on_started() override {
        konative::core::log_info(
            "KonativeAndroidApp: on_started (World constructed, first real Activity created)");

        // ARCHITECTURE.md section 9's one dependency in this stack with "no confirmed track record
        // either way" on Android NDK specifically (every other dependency there has real cited
        // precedent) - runs once at real startup, on the real device, not just at desktop-debug
        // build time. A real, permanent regression guard against a future NDK/toolchain upgrade
        // silently breaking Taskflow's threading on this target, not a one-off spike thrown away
        // after proving it once.
        bool taskflow_ok = konative::scheduling::run_taskflow_self_check();
        if (taskflow_ok) {
            konative::core::log_info(
                "KonativeAndroidApp: Taskflow self-check PASSED on this device/ABI - real parallel "
                "task scheduling confirmed correct (ARCHITECTURE.md section 9's previously "
                "unconfirmed risk).");
        } else {
            konative::core::log_error(
                "KonativeAndroidApp: Taskflow self-check FAILED on this device/ABI - real parallel "
                "task scheduling produced a wrong result. This does not affect Compose rendering "
                "(nothing else in this app currently depends on Taskflow), but confirms "
                "ARCHITECTURE.md section 9's flagged risk is real on this specific target - do not "
                "build further architecture on Taskflow here without investigating first.");
        }

        // Real ECS proof, the one piece Dispatcher/events + the tick driver + Taskflow above didn't
        // cover: create real entities with a real component, register a real system the standard
        // way. on_tick() below reports the real, live result every kTickLogInterval frames.
        auto& registry = world().registry();
        for (int i = 0; i < kHeartbeatEntityCount; ++i) {
            registry.emplace<HeartbeatCounter>(registry.create());
        }
        world().systems().add(&increment_heartbeat_counters);
        konative::core::log_info(
            "KonativeAndroidApp: {} real ECS entities created (each with a HeartbeatCounter "
            "component), one real system registered to increment them every tick.",
            kHeartbeatEntityCount);

        // Resolves the other "defined since early on, never actually dispatched" open item:
        // events/input/Touch{Down,Move,Up}Event.hpp existed with nothing anywhere in the codebase
        // ever triggering them (confirmed by grep before writing this) until now.
        // KonativeEntryPoint.kt's new Modifier.pointerInput block below is the real producer; these
        // are real entt::dispatcher sinks, the same connection mechanism every other event consumer
        // in this codebase already uses (see test_events.cpp/test_cross_thread_event_queue.cpp) -
        // *this is a stable address for the sink to bind to (android_app() is a function-local
        // static singleton, alive for the whole process).
        world().events().sink<konative::events::input::TouchDownEvent>().connect<&KonativeAndroidApp::on_touch_down>(*this);
        world().events().sink<konative::events::input::TouchMoveEvent>().connect<&KonativeAndroidApp::on_touch_move>(*this);
        world().events().sink<konative::events::input::TouchUpEvent>().connect<&KonativeAndroidApp::on_touch_up>(*this);
    }
    void on_resumed() override { konative::core::log_info("KonativeAndroidApp: on_resumed"); }
    void on_paused() override { konative::core::log_info("KonativeAndroidApp: on_paused"); }
    void on_destroyed() override { konative::core::log_info("KonativeAndroidApp: on_destroyed"); }

    // Logs a summary every kTickLogInterval frames rather than every single tick (Choreographer
    // ticks once per display refresh, ~60-120Hz on real hardware - logging every occurrence would
    // flood logcat for no diagnostic benefit, the same "downgrade high-frequency routine noise to a
    // periodic summary" lesson this codebase has already re-learned once for a different kind of
    // per-frame spam). Real proof this is alive: tick_count_ only advances while Choreographer is
    // actually posting frames, i.e. only while the Activity is genuinely resumed.
    void on_tick(float delta_seconds) override {
        ++tick_count_;
        if (tick_count_ % kTickLogInterval == 0) {
            // world_.tick() (called before on_tick(), see Application::tick()) already ran
            // increment_heartbeat_counters this frame - reading the view here proves the real
            // Registry/View/System pipeline is genuinely operating, not just that emplace() didn't
            // throw at startup: entity_count should read kHeartbeatEntityCount exactly, and
            // total_heartbeat_ticks should read kHeartbeatEntityCount*tick_count_ exactly (every
            // entity incremented once per tick, every tick, since on_started()).
            std::uint64_t total_heartbeat_ticks = 0;
            std::size_t entity_count = 0;
            for (auto [entity, counter] : world().registry().view<HeartbeatCounter>().each()) {
                total_heartbeat_ticks += counter.ticks;
                ++entity_count;
            }
            konative::core::log_info(
                "KonativeAndroidApp: on_tick - {} real frames delivered so far (last delta {:.4f}s); "
                "real ECS proof: {} entities, {} combined HeartbeatCounter ticks",
                tick_count_, delta_seconds, entity_count, total_heartbeat_ticks);
        }
    }

    // Read from the SAME thread that writes it (both on_tick() and the Kotlin-side query below run
    // on the real Activity's main/UI thread - Choreographer callbacks and native-method calls from
    // Kotlin's own main thread are both synchronous on that one thread) - a plain, non-atomic read
    // is genuinely safe here, not just convenient; no cross-thread synchronization needed.
    [[nodiscard]] std::uint64_t tick_count() const { return tick_count_; }

    // Same same-thread reasoning as tick_count() above - real touch dispatch (native_dispatch_touch_*
    // below) and this read both happen on the Activity's main/UI thread only.
    [[nodiscard]] std::uint64_t touch_event_count() const { return touch_event_count_; }

private:
    // Logs every occurrence, unlike on_tick()'s periodic summary above - real touches are
    // discrete/user-paced (nowhere near 60-120Hz), so there's no flood risk, and seeing every one
    // (with its real coordinates) is exactly the point of a first on-device verification pass.
    void on_touch_down(const konative::events::input::TouchDownEvent& event) {
        ++touch_event_count_;
        konative::core::log_info(
            "KonativeAndroidApp: TouchDownEvent pointer={} x={:.1f} y={:.1f} (total touch events: {})",
            event.pointer_id, event.x, event.y, touch_event_count_);
    }
    void on_touch_move(const konative::events::input::TouchMoveEvent& event) {
        ++touch_event_count_;
        konative::core::log_info(
            "KonativeAndroidApp: TouchMoveEvent pointer={} x={:.1f} y={:.1f} (total touch events: {})",
            event.pointer_id, event.x, event.y, touch_event_count_);
    }
    void on_touch_up(const konative::events::input::TouchUpEvent& event) {
        ++touch_event_count_;
        konative::core::log_info(
            "KonativeAndroidApp: TouchUpEvent pointer={} x={:.1f} y={:.1f} (total touch events: {})",
            event.pointer_id, event.x, event.y, touch_event_count_);
    }

    static constexpr std::uint64_t kTickLogInterval = 120; // ~1-2s of real frames on typical hardware
    static constexpr int kHeartbeatEntityCount = 3;
    std::uint64_t tick_count_ = 0;
    std::uint64_t touch_event_count_ = 0;
};

// Function-local static, same singleton-via-static pattern examples/minimal_triangle/main.cpp's
// own create_application() already uses - constructed lazily on first call (JNI_OnLoad's own
// RegisterNatives below never calls this directly; the first real call happens later, whenever the
// embedded dex's first real Activity lifecycle event actually fires). Returns the concrete type,
// not just konative::app::Application&, so native_get_tick_count() below can reach tick_count() -
// create_application() (entry_point.hpp's real public contract) still returns the base reference,
// unaffected.
KonativeAndroidApp& android_app() {
    static KonativeAndroidApp instance;
    return instance;
}

// The 4 lifecycle transitions konative::app::Application models, in the exact int encoding
// KonativeEntryPoint.kt's own nativeDispatchLifecycle(Int) contract expects - both sides must stay
// in sync (same discipline as install()'s own signature contract below/in
// embedded_kotlin/README.md's Hard Rule). No "stopped" id exists because konative::app::Application
// itself has no separate stop() method (see application.hpp) - onActivityStopped stays scoped to
// Jetpack's own Lifecycle system only, unchanged by this feature.
enum class AndroidLifecycleEvent : jint {
    kStarted = 0,
    kResumed = 1,
    kPaused = 2,
    kDestroyed = 3,
};

// The real function RegisterNatives binds to KonativeEntryPoint.nativeDispatchLifecycle(Int) below
// - called FROM Kotlin, the opposite direction from install() above, so (unlike install()) this
// call site IS visible to R8's own static call graph and doesn't need a dedicated -keep rule for
// shrinking; embedded_kotlin/r8-rules.pro still keeps native-method NAMES generically (the
// standard Android idiom) as cheap insurance against a future re-enabled obfuscation pass.
void JNICALL native_dispatch_lifecycle_event(JNIEnv*, jclass, jint event) {
    auto& app = android_app();
    switch (static_cast<AndroidLifecycleEvent>(event)) {
        case AndroidLifecycleEvent::kStarted:
            app.start();
            return;
        case AndroidLifecycleEvent::kResumed:
            app.resume();
            return;
        case AndroidLifecycleEvent::kPaused:
            app.pause();
            return;
        case AndroidLifecycleEvent::kDestroyed:
            app.destroy();
            return;
    }
    konative::core::log_error("native_dispatch_lifecycle_event: unknown event id {}",
                               static_cast<int>(event));
}

// Bound to KonativeEntryPoint.nativeTick(Float) - one real per-frame heartbeat for the C++
// ECS/systems side (World::tick() -> SystemSequence::run() + Dispatcher::update(), flushing any
// enqueued - as opposed to triggered - events queued since the last frame). See
// KonativeEntryPoint.kt's Choreographer.FrameCallback loop for where deltaSeconds is computed and
// why this only runs while the Activity is genuinely resumed.
void JNICALL native_dispatch_tick(JNIEnv*, jclass, jfloat delta_seconds) {
    android_app().tick(delta_seconds);
}

// Bound to KonativeEntryPoint.nativeGetTickCount() - lets real, rendered Compose UI display real
// C++-side state for the first time, closing the loop from "the C++ ECS/systems side ticks, but
// only logcat ever sees it" to something actually visible on screen. Read-only, deliberately: this
// is a query, not another event - it doesn't go through events::Dispatcher at all.
jlong JNICALL native_get_tick_count(JNIEnv*, jclass) {
    return static_cast<jlong>(android_app().tick_count());
}

// Bound to KonativeEntryPoint.nativeDispatchTouchDown/Move/Up(Int, Float, Float) - the real producer
// side of events/input/Touch{Down,Move,Up}Event.hpp, which existed with nothing anywhere dispatching
// them until now (see KonativeAndroidApp::on_started()'s sink connections above). trigger(), not
// enqueue() - immediate synchronous dispatch, the same choice lifecycle events already made
// (dispatcher.hpp's own doc comment), appropriate here too since KonativeEntryPoint.kt's
// Modifier.pointerInput block calling these and World::tick()'s own Dispatcher::update() both run on
// the same Activity main/UI thread - no cross-thread concern, no reason to defer a frame.
void JNICALL native_dispatch_touch_down(JNIEnv*, jclass, jint pointer_id, jfloat x, jfloat y) {
    android_app().world().events().trigger(
        konative::events::input::TouchDownEvent{static_cast<std::int32_t>(pointer_id), x, y});
}
void JNICALL native_dispatch_touch_move(JNIEnv*, jclass, jint pointer_id, jfloat x, jfloat y) {
    android_app().world().events().trigger(
        konative::events::input::TouchMoveEvent{static_cast<std::int32_t>(pointer_id), x, y});
}
void JNICALL native_dispatch_touch_up(JNIEnv*, jclass, jint pointer_id, jfloat x, jfloat y) {
    android_app().world().events().trigger(
        konative::events::input::TouchUpEvent{static_cast<std::int32_t>(pointer_id), x, y});
}

// Bound to KonativeEntryPoint.nativeGetTouchCount() - same "let real Compose UI show real C++-side
// state" pattern as native_get_tick_count() above, for the new touch-event counter.
jlong JNICALL native_get_touch_count(JNIEnv*, jclass) {
    return static_cast<jlong>(android_app().touch_event_count());
}

} // namespace

namespace konative::app {

// The one real create_application() this binary defines (entry_point.hpp's own contract - exactly
// once per binary, whatever platform glue is compiled in constructs/drives it).
Application& create_application() { return android_app(); }

} // namespace konative::app

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr) {
        konative::core::log_error("JNI_OnLoad: GetEnv failed");
        return JNI_VERSION_1_6;
    }

    // Step 1: self-check the embedded blob (ARCHITECTURE.md section 6.5) BEFORE trusting it -
    // catches "the build pipeline embedded the wrong/truncated bytes" as a clear, actionable
    // startup error instead of a mysterious crash deep inside the classloader.
    std::span<const unsigned char> dex_bytes(konative_app_dex_start,
                                              static_cast<std::size_t>(konative_app_dex_size));
    auto verified = konative::embed::verify_blob(dex_bytes, konative_app_dex_expected_sha256);
    if (!verified) {
        konative::core::log_error(
            "JNI_OnLoad: embedded dex blob failed its SHA-256 self-check - the build pipeline "
            "embedded the wrong or truncated bytes. Refusing to load it.");
        return JNI_VERSION_1_6;
    }

    // Steps 2-5: getClassLoader() -> NewDirectByteBuffer -> InMemoryDexClassLoader -> loadClass()
    // (konative::jni::load_class_from_dex, ARCHITECTURE.md section 6.6) - this is also where the
    // ONE hidden-API call in this whole design happens (ActivityThread.currentApplication()).
    auto loaded = konative::jni::load_class_from_dex(env, verified.value().data(),
                                                       verified.value().size(), kEntryPointClass);
    if (!loaded) {
        konative::core::log_error(
            "JNI_OnLoad: failed to load {} from the embedded dex (step {})", kEntryPointClass,
            static_cast<int>(loaded.error()));
        return JNI_VERSION_1_6;
    }

    // Step 5.5: self-check + hand off the sibling resources.arsc blob - NON-fatal on failure,
    // unlike the dex blob's own self-check above: this only feeds an optional runtime enhancement
    // (embedded_kotlin/KonativeResourcesLoader.kt's API-30+ ResourcesLoader mechanism), never the
    // core load path, so a corrupted/missing copy degrades to Kotlin receiving `null` and falling
    // back to the always-available scoped patch (embedded_kotlin/KonativeResourceStringOverride
    // .kt) instead of refusing to load the whole app the dex blob's own failure would warrant.
    std::span<const unsigned char> resources_bytes(
        konative_app_dex_resources_start,
        static_cast<std::size_t>(konative_app_dex_resources_size));
    auto verified_resources =
        konative::embed::verify_blob(resources_bytes, konative_app_dex_resources_expected_sha256);
    jobject resources_buffer = nullptr;
    if (!verified_resources) {
        konative::core::log_error(
            "JNI_OnLoad: embedded resources.arsc blob failed its SHA-256 self-check - continuing "
            "without it (Kotlin falls back to the scoped resource-string override).");
    } else {
        resources_buffer = env->NewDirectByteBuffer(
            const_cast<unsigned char*>(verified_resources.value().data()),
            static_cast<jlong>(verified_resources.value().size()));
        if (konative::jni::check_and_clear_exception(env, "NewDirectByteBuffer(resources.arsc)") ||
            resources_buffer == nullptr) {
            konative::core::log_error(
                "JNI_OnLoad: NewDirectByteBuffer(resources.arsc) failed - continuing without it.");
            resources_buffer = nullptr;
        }
    }

    // Step 5.6: bind Kotlin's nativeDispatchLifecycle(Int)/nativeTick(Float)/nativeGetTickCount()/
    // nativeDispatchTouch{Down,Move,Up}(Int,Float,Float)/nativeGetTouchCount() to the functions above
    // - see KonativeAndroidApp's own comment for why. Deliberately non-fatal on failure, same as the
    // resources.arsc blob above: this wires a real feature (the C++ ECS/events core actually
    // running, real touch input reaching it, and both states actually visible in the rendered UI),
    // but its absence doesn't prevent Compose from rendering - only degrades to the C++ side never
    // seeing lifecycle transitions/ticks/touches, and the UI never showing live counts. Must happen
    // before install() below even though the very first real call can't arrive until later (an
    // Activity lifecycle event firing, or Choreographer's first posted frame, are both asynchronous,
    // driven by the real Android framework afterward) - registering natives before handoff is simply
    // the cleaner ordering, with no dependency on exactly when Kotlin's first callback fires.
    JNINativeMethod native_methods[] = {
        {
            const_cast<char*>("nativeDispatchLifecycle"),
            const_cast<char*>("(I)V"),
            reinterpret_cast<void*>(&native_dispatch_lifecycle_event),
        },
        {
            const_cast<char*>("nativeTick"),
            const_cast<char*>("(F)V"),
            reinterpret_cast<void*>(&native_dispatch_tick),
        },
        {
            const_cast<char*>("nativeGetTickCount"),
            const_cast<char*>("()J"),
            reinterpret_cast<void*>(&native_get_tick_count),
        },
        {
            const_cast<char*>("nativeDispatchTouchDown"),
            const_cast<char*>("(IFF)V"),
            reinterpret_cast<void*>(&native_dispatch_touch_down),
        },
        {
            const_cast<char*>("nativeDispatchTouchMove"),
            const_cast<char*>("(IFF)V"),
            reinterpret_cast<void*>(&native_dispatch_touch_move),
        },
        {
            const_cast<char*>("nativeDispatchTouchUp"),
            const_cast<char*>("(IFF)V"),
            reinterpret_cast<void*>(&native_dispatch_touch_up),
        },
        {
            const_cast<char*>("nativeGetTouchCount"),
            const_cast<char*>("()J"),
            reinterpret_cast<void*>(&native_get_touch_count),
        },
    };
    if (env->RegisterNatives(loaded.value().clazz.get(), native_methods, 7) != JNI_OK ||
        konative::jni::check_and_clear_exception(env, "RegisterNatives(nativeDispatchLifecycle/nativeTick/nativeGetTickCount/nativeDispatchTouch*/nativeGetTouchCount)")) {
        konative::core::log_error(
            "JNI_OnLoad: RegisterNatives(nativeDispatchLifecycle/nativeTick/nativeGetTickCount/"
            "nativeDispatchTouch*/nativeGetTouchCount) failed - the C++ ECS/events core will not "
            "receive real Activity lifecycle transitions, ticks, or touches this run, and the UI "
            "will not show live tick/touch counts. Compose rendering is otherwise unaffected (these "
            "native methods are unrelated to install()'s own handoff below); Kotlin's own call sites "
            "are individually try/caught for exactly this case, see KonativeEntryPoint.kt.");
    }

    // Step 6: ONE handoff call, then native code is done - everything past this point (Compose
    // wiring, ActivityLifecycleCallbacks) is real, compiled Kotlin (ARCHITECTURE.md section 6.4).
    jmethodID install = env->GetStaticMethodID(loaded.value().clazz.get(), "install",
                                                 "(Landroid/app/Application;Ljava/nio/ByteBuffer;)V");
    if (konative::jni::check_and_clear_exception(env, "GetStaticMethodID(install)") ||
        install == nullptr) {
        konative::core::log_error("JNI_OnLoad: {} has no install(Application, ByteBuffer) static method",
                                   kEntryPointClass);
        return JNI_VERSION_1_6;
    }
    env->CallStaticVoidMethod(loaded.value().clazz.get(), install, loaded.value().application.get(),
                               resources_buffer);
    konative::jni::check_and_clear_exception(env, "CallStaticVoidMethod(install)");

    // `loaded.value()`'s GlobalRefs (clazz, application) are deliberately never reset(env)'d here -
    // unlike ref.hpp's general "MUST be released explicitly" rule, this is the ONE intentional
    // exception: keeping the loaded class globally referenced is what pins it (and transitively its
    // ClassLoader) alive for the rest of the process's life, which is exactly the desired behavior
    // for a load-once native entry point - a verification pass on commit 3618fb5 flagged this as
    // worth documenting explicitly, since it reads as a leak against jni/README.md's stated rule
    // without this note.
    return JNI_VERSION_1_6;
}
