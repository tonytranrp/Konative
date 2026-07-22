package com.konative.generated

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.changedToDown
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.setViewTreeLifecycleOwner
import androidx.lifecycle.setViewTreeViewModelStoreOwner
import androidx.savedstate.SavedStateRegistry
import androidx.savedstate.SavedStateRegistryController
import androidx.savedstate.SavedStateRegistryOwner
import androidx.savedstate.setViewTreeSavedStateRegistryOwner
import java.nio.ByteBuffer

// The one entry point src/platform/android/jni_onload.cpp's InMemoryDexClassLoader-loaded class
// resolves and calls (ARCHITECTURE.md section 6.4 step 3 / research/jni_activity_bootstrap_research.md
// section 5.2 - implemented against that design directly, not re-derived). Everything past
// JNI_OnLoad's one CallStaticVoidMethod handoff is real, compiled Kotlin from here on - no further
// JNI reflection, per this project's own "favor real Kotlin over reflection" rule.
//
// install(Application, ByteBuffer?)'s SECOND parameter (added alongside the general resources.arsc
// mechanism - see KonativeResourcesLoader.kt) is the embedded resources.arsc blob, or null if its
// own SHA-256 self-check failed at JNI_OnLoad - jni_onload.cpp's own kEntryPointClass contract note
// must stay in sync with this exact signature, per embedded_kotlin/README.md's Hard Rule.
object KonativeEntryPoint {
    @JvmStatic
    fun install(application: Application, resourcesArscBuffer: ByteBuffer?) {
        application.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            private var owner: ComposeHostOwner? = null

            // Tracks the native STARTED dispatch (real World/ECS construction, every startup
            // self-check, entity creation - jni_onload.cpp's own on_started()) SEPARATELY from
            // owner/ComposeView installation below. The native side is only ever meant to run that
            // one-time setup once per PROCESS, not once per Activity INSTANCE - but a real device
            // rotation destroys and recreates the Activity (a fresh instance, a fresh, empty
            // content view) while the process itself keeps running. A single `owner != null` guard
            // covering both concerns was a real, critical bug (found by a 2026-07-22 deep review,
            // reproduced by reasoning through testapp/AndroidManifest.xml's real manifest - no
            // configChanges/screenOrientation override, so a plain rotation genuinely destroys and
            // recreates MainActivity): rotating the device even once left `owner` pointing at the
            // now-dead instance's ComposeHostOwner, so the guard silently skipped installing
            // anything on every Activity created after the first - the entire Compose UI vanished
            // permanently for the rest of the process's life, with zero error/log signal, while the
            // native tick/touch/lifecycle machinery kept running underneath as if nothing happened.
            private var nativeStarted = false

            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
                if (!nativeStarted) {
                    nativeStarted = true
                    dispatchToNative(AndroidLifecycleEvent.STARTED)
                }

                // Runs for EVERY Activity instance, including a post-rotation recreation - each one
                // gets its own fresh ComposeHostOwner/ComposeView, matching the fact each one starts
                // with no content view of its own (the previous instance's view tree was destroyed
                // along with it).
                owner = ComposeHostOwner().apply { performRestore(savedInstanceState) }
                owner!!.registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)

                val composeContext = installResourcesAndGetComposeContext(activity, resourcesArscBuffer)

                val composeView = ComposeView(composeContext).apply {
                    setViewTreeLifecycleOwner(owner)
                    setViewTreeViewModelStoreOwner(owner)
                    setViewTreeSavedStateRegistryOwner(owner)
                    setContent { KonativeRootComposable() }
                }
                activity.setContentView(composeView)
            }

            override fun onActivityStarted(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_START)
            }

            override fun onActivityResumed(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)
                dispatchToNative(AndroidLifecycleEvent.RESUMED)
                FrameTicker.start()
            }

            override fun onActivityPaused(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)
                FrameTicker.stop()
                dispatchToNative(AndroidLifecycleEvent.PAUSED)
            }

            override fun onActivityStopped(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            }

            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {
                owner?.performSave(outState)
            }

            override fun onActivityDestroyed(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_DESTROY)
                // MUST reset to null here, or the NEXT Activity instance's onActivityCreated (a
                // real recreation, e.g. after rotation) would see a stale non-null owner and - back
                // when a single guard covered both concerns - would incorrectly skip installing a
                // ComposeView on it. Now purely a hygiene reset (nativeStarted above is what
                // actually gates re-dispatching STARTED), but still correct to clear: nothing should
                // hold a reference to a destroyed Activity's owner past this point.
                owner = null
                dispatchToNative(AndroidLifecycleEvent.DESTROYED)
            }
        })
    }

    // Native, JNI_OnLoad-bound counterpart to install()'s own JNI contract, but the opposite call
    // direction (Kotlin -> native, not native -> Kotlin) - see jni_onload.cpp's own comment on
    // RegisterNatives for the full design. Int encoding must stay in sync with jni_onload.cpp's
    // AndroidLifecycleEvent enum exactly (see AndroidLifecycleEvent below).
    @JvmStatic
    external fun nativeDispatchLifecycle(event: Int)

    // Same RegisterNatives contract, bound to native_dispatch_tick - one real per-frame heartbeat
    // for the C++ ECS/systems side, driven by FrameTicker below.
    @JvmStatic
    external fun nativeTick(deltaSeconds: Float)

    // Same RegisterNatives contract, bound to native_get_tick_count - a read-only query (not
    // another event) letting real, rendered Compose UI display real C++-side state for the first
    // time. See KonativeRootComposable's own use of tickCountDisplay below.
    @JvmStatic
    external fun nativeGetTickCount(): Long

    // Same RegisterNatives contract, bound to native_dispatch_touch_{down,move,up} - the real
    // producer side of events/input/Touch{Down,Move,Up}Event.hpp (jni_onload.cpp's own comment has
    // the full writeup of why these existed with nothing dispatching them until now). Driven by
    // KonativeRootComposable's Modifier.pointerInput block below. pointerId is PointerInputChange.id
    // .value truncated to Int - TouchDownEvent/TouchMoveEvent/TouchUpEvent all model it as
    // std::int32_t (jni_onload.cpp), matching every other event struct's plain-value-type convention
    // rather than carrying a 64-bit id no consumer needs yet.
    @JvmStatic
    external fun nativeDispatchTouchDown(pointerId: Int, x: Float, y: Float)
    @JvmStatic
    external fun nativeDispatchTouchMove(pointerId: Int, x: Float, y: Float)
    @JvmStatic
    external fun nativeDispatchTouchUp(pointerId: Int, x: Float, y: Float)

    // Same RegisterNatives contract, bound to native_get_touch_count - same "let real Compose UI
    // show real C++-side state" pattern as nativeGetTickCount() above.
    @JvmStatic
    external fun nativeGetTouchCount(): Long

    // Same RegisterNatives contract, bound to native_dispatch_window_resized/
    // native_dispatch_window_focus_changed - real producers are KonativeRootComposable's
    // Modifier.onSizeChanged and a LaunchedEffect reading LocalWindowInfo.current.isWindowFocused
    // (see jni_onload.cpp's own on_started() comment for why these two specifically, and not
    // WindowCreatedEvent/WindowDestroyedEvent, were wired).
    @JvmStatic
    external fun nativeDispatchWindowResized(width: Int, height: Int)
    @JvmStatic
    external fun nativeDispatchWindowFocusChanged(hasFocus: Boolean)

    // Same RegisterNatives contract, bound to native_dispatch_key_event - the last unwired
    // events/input/ type (jni_onload.cpp's own on_started() comment has the full writeup: real
    // hardware key presses, volume up/down specifically since they exist on every real device).
    // Real producer: KonativeRootComposable's Modifier.onKeyEvent.
    @JvmStatic
    external fun nativeDispatchKeyEvent(keyCode: Int, isDown: Boolean)
}

// Backs KonativeRootComposable's live tick-count display - a plain, object-scoped
// androidx.compose.runtime.State, not one created via remember{} inside a composable, since it's
// written from FrameTicker (real code, not composition) and just needs to be READ inside a
// composable to correctly trigger recomposition on change; Compose doesn't require State objects
// driving a composable to themselves be remember{}-scoped.
private var tickCountDisplay by mutableStateOf(0L)

// Backs KonativeRootComposable's live touch-count display - same object-scoped State shape as
// tickCountDisplay above, for the same reason (written from the pointerInput pointer-event loop
// below, which is real code, not composition).
private var touchCountDisplay by mutableStateOf(0L)

// Mirrors jni_onload.cpp's own AndroidLifecycleEvent enum exactly - both sides must stay in sync.
private object AndroidLifecycleEvent {
    const val STARTED = 0
    const val RESUMED = 1
    const val PAUSED = 2
    const val DESTROYED = 3
}

// Backs logNativeBindingFailureOnce() below - a real 2026-07-22 deep review found every wrapper in
// this file logged on EVERY occurrence with no throttling, and two of them (dispatchTickToNative/
// queryTickCountFromNative) are called from FrameTicker.doFrame(), i.e. up to 60-120 times per
// second for as long as the Activity stays resumed - a real signature mismatch after a future
// refactor would silently degrade from "one clear error" into a logcat flood, itself risking masking
// other diagnostics and adding real per-call I/O jank on doFrame()'s otherwise-lightweight hot path.
private val loggedNativeBindingFailures = mutableSetOf<String>()

// Logs a native-binding failure exactly once per distinct call site (keyed by `tag`, not by message
// content, so a transient detail like an event/count value in the message doesn't defeat the
// dedup) for the remaining life of the process - still guarantees the developer sees it at least
// once, matching this codebase's "report clearly, don't silently swallow" standing rule
// (embedded_kotlin/README.md, core/log.hpp's own doc comment), without flooding logcat on a hot path.
// Not thread-safe (a plain MutableSet, no synchronization) - correct here because every real call
// site below is only ever reached from the main/Choreographer/Compose thread (verified by tracing
// each one's real producer during the same review), not because it's an oversight.
private fun logNativeBindingFailureOnce(tag: String, message: String, throwable: Throwable) {
    if (loggedNativeBindingFailures.add(tag)) {
        Log.e("Konative", message, throwable)
    }
}

// Wraps every real nativeDispatchLifecycle() call site: if RegisterNatives ever failed to bind this
// method (jni_onload.cpp's own JNI_OnLoad logs why, non-fatally, if so), an uncaught
// UnsatisfiedLinkError here would crash the Activity's own onResume()/onPause()/onDestroy() -
// breaking real Compose rendering over a C++-side feature that's supposed to degrade gracefully.
private fun dispatchToNative(event: Int) {
    try {
        KonativeEntryPoint.nativeDispatchLifecycle(event)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchToNative",
            "dispatchToNative: nativeDispatchLifecycle unavailable (event=$event) - " +
                "the C++ ECS/events core will not see this transition; Compose is unaffected.", e)
    }
}

// Drives konative::app::Application::tick() once per real display frame while (and only while) the
// Activity is genuinely resumed - started/stopped from onActivityResumed/onActivityPaused above,
// alongside the existing lifecycle dispatch. A SEPARATE mechanism from Compose's own frame
// scheduling (Compose still owns real rendering) - this exists purely to give the C++ ECS/systems
// side a real per-frame heartbeat, closing the "World::tick() has no natural driver" gap
// ARCHITECTURE.md's own status table used to note as still open.
private object FrameTicker : Choreographer.FrameCallback {
    private var active = false
    private var lastFrameTimeNanos = 0L

    fun start() {
        if (active) return
        active = true
        lastFrameTimeNanos = 0L // reset so the delta right after a resume isn't a stale huge gap
        Choreographer.getInstance().postFrameCallback(this)
    }

    // DOES call Choreographer.removeFrameCallback(this) - a real 2026-07-22 deep review found the
    // earlier "just flip the flag, the stale callback's own `if (!active) return` makes it a clean
    // no-op" reasoning only holds if start() isn't called again before that stale callback fires.
    // If stop()+start() happen twice within one frame interval (e.g. a fast pause/resume pair during
    // Activity recreation - the same rotation path Finding 1 above fixes), the still-queued callback
    // from before stop() and the new one posted by start() both end up pending, and BOTH fire at the
    // next vsync (Choreographer doesn't deduplicate by callback instance) - permanently doubling the
    // native tick rate for the rest of the Activity's life, silently, with tickCountDisplay visibly
    // incrementing by 2 per frame instead of 1. Actually cancelling the pending callback here closes
    // that window instead of relying on timing (verified real API via javap against android.jar).
    fun stop() {
        active = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!active) return
        val deltaSeconds = if (lastFrameTimeNanos == 0L) {
            0f // first frame after start(): no real previous timestamp yet, report 0 not a bogus gap
        } else {
            (frameTimeNanos - lastFrameTimeNanos) / 1_000_000_000f
        }
        lastFrameTimeNanos = frameTimeNanos
        dispatchTickToNative(deltaSeconds)
        // Updated every real frame, not throttled the way jni_onload.cpp's own logcat summary is -
        // a log line has real per-call I/O cost worth batching; recomposing one small BasicText at
        // real display refresh rate is the normal, lightweight case Compose is built for, and doing
        // it every frame is what makes this genuinely "live" rather than merely periodic.
        tickCountDisplay = queryTickCountFromNative()
        Choreographer.getInstance().postFrameCallback(this)
    }
}

// Same try/catch-wrapped shape as dispatchToNative() above, for the same reason - see that
// function's own comment.
private fun dispatchTickToNative(deltaSeconds: Float) {
    try {
        KonativeEntryPoint.nativeTick(deltaSeconds)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchTickToNative",
            "dispatchTickToNative: nativeTick unavailable - the C++ ECS/systems side " +
                "will not receive real per-frame ticks; Compose rendering is unaffected.", e)
    }
}

// Same try/catch-wrapped shape, for the same reason - a missing native binding degrades to the UI
// simply not advancing its tick display (stuck at its last real value, or 0 if it never got one),
// rather than crashing the per-frame callback loop.
private fun queryTickCountFromNative(): Long {
    return try {
        KonativeEntryPoint.nativeGetTickCount()
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("queryTickCountFromNative",
            "queryTickCountFromNative: nativeGetTickCount unavailable - the UI will " +
                "not show a live tick count; Compose rendering is otherwise unaffected.", e)
        tickCountDisplay
    }
}

// Same try/catch-wrapped shape as dispatchTickToNative() above, for the same reason - driven by
// KonativeRootComposable's Modifier.pointerInput block below, one call per real down/move/up change.
private fun dispatchTouchDownToNative(pointerId: Int, x: Float, y: Float) {
    try {
        KonativeEntryPoint.nativeDispatchTouchDown(pointerId, x, y)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchTouchDownToNative",
            "dispatchTouchDownToNative: nativeDispatchTouchDown unavailable - the " +
                "C++ ECS/events core will not see this touch; Compose rendering is unaffected.", e)
    }
}
private fun dispatchTouchMoveToNative(pointerId: Int, x: Float, y: Float) {
    try {
        KonativeEntryPoint.nativeDispatchTouchMove(pointerId, x, y)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchTouchMoveToNative",
            "dispatchTouchMoveToNative: nativeDispatchTouchMove unavailable - the " +
                "C++ ECS/events core will not see this touch; Compose rendering is unaffected.", e)
    }
}
private fun dispatchTouchUpToNative(pointerId: Int, x: Float, y: Float) {
    try {
        KonativeEntryPoint.nativeDispatchTouchUp(pointerId, x, y)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchTouchUpToNative",
            "dispatchTouchUpToNative: nativeDispatchTouchUp unavailable - the C++ " +
                "ECS/events core will not see this touch; Compose rendering is unaffected.", e)
    }
}

// Same try/catch-wrapped shape as queryTickCountFromNative() above, for the same reason.
private fun queryTouchCountFromNative(): Long {
    return try {
        KonativeEntryPoint.nativeGetTouchCount()
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("queryTouchCountFromNative",
            "queryTouchCountFromNative: nativeGetTouchCount unavailable - the UI " +
                "will not show a live touch count; Compose rendering is otherwise unaffected.", e)
        touchCountDisplay
    }
}

// Same try/catch-wrapped shape as dispatchTouchDownToNative() above, for the same reason - driven
// by KonativeRootComposable's Modifier.onSizeChanged.
private fun dispatchWindowResizedToNative(width: Int, height: Int) {
    try {
        KonativeEntryPoint.nativeDispatchWindowResized(width, height)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchWindowResizedToNative",
            "dispatchWindowResizedToNative: nativeDispatchWindowResized unavailable " +
                "- the C++ ECS/events core will not see this resize; Compose rendering is unaffected.", e)
    }
}

// Same try/catch-wrapped shape, driven by KonativeRootComposable's LaunchedEffect reading
// LocalWindowInfo.current.isWindowFocused.
private fun dispatchWindowFocusChangedToNative(hasFocus: Boolean) {
    try {
        KonativeEntryPoint.nativeDispatchWindowFocusChanged(hasFocus)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchWindowFocusChangedToNative",
            "dispatchWindowFocusChangedToNative: nativeDispatchWindowFocusChanged " +
                "unavailable - the C++ ECS/events core will not see this focus change; Compose " +
                "rendering is unaffected.", e)
    }
}

// Same try/catch-wrapped shape, driven by KonativeRootComposable's Modifier.onKeyEvent.
private fun dispatchKeyEventToNative(keyCode: Int, isDown: Boolean) {
    try {
        KonativeEntryPoint.nativeDispatchKeyEvent(keyCode, isDown)
    } catch (e: UnsatisfiedLinkError) {
        logNativeBindingFailureOnce("dispatchKeyEventToNative",
            "dispatchKeyEventToNative: nativeDispatchKeyEvent unavailable - the C++ " +
                "ECS/events core will not see this key event; Compose rendering is unaffected.", e)
    }
}

// Selects between the two real resources.arsc mechanisms (see embedded_kotlin/README.md's Update
// sections for the full writeup of both): the general, API-30+ ResourcesLoader mechanism
// (KonativeResourcesLoader.kt) if it installs successfully - which mutates the real Activity's own
// Resources in place, so the real, unwrapped `activity` is returned as-is, no wrapping needed - or
// KonativeResourceStringOverride.kt's smaller, scoped Context/Resources-wrapper patch as the
// fallback (API <30, or the general mechanism failed for any reason at runtime). Only runs the
// scoped patch's OWN self-check in the fallback branch - the general mechanism's real, localized
// values would legitimately not match that self-check's hardcoded English expectations, even when
// working correctly.
private fun installResourcesAndGetComposeContext(activity: Activity, resourcesArscBuffer: ByteBuffer?): Context {
    if (tryInstallGeneralResourcesLoader(activity, resourcesArscBuffer)) {
        return activity
    }
    val resourceOverrideContext = wrapContextForResourceStringOverride(activity)
    konativeResourceStringOverrideSelfCheck(resourceOverrideContext)
    return resourceOverrideContext
}

// Fabricates the LifecycleOwner/ViewModelStoreOwner/SavedStateRegistryOwner trio a plain Activity
// doesn't provide automatically (research/jni_activity_bootstrap_research.md section 3.1/3.2) -
// ComposeView.setContent() requires all three to be reachable from the view tree.
private class ComposeHostOwner : LifecycleOwner, ViewModelStoreOwner, SavedStateRegistryOwner {
    val registry = LifecycleRegistry(this)
    override val lifecycle: Lifecycle get() = registry
    override val viewModelStore = ViewModelStore()
    // performRestore()/performSave() live on the CONTROLLER, not on LifecycleRegistry - an earlier
    // draft of this file called them on `registry` (matching a research-doc sketch that was never
    // actually compiled) and failed with "unresolved reference": real compilation caught it.
    private val savedStateRegistryController = SavedStateRegistryController.create(this)
    override val savedStateRegistry: SavedStateRegistry get() = savedStateRegistryController.savedStateRegistry

    fun performRestore(savedState: Bundle?) = savedStateRegistryController.performRestore(savedState)
    fun performSave(outState: Bundle) = savedStateRegistryController.performSave(outState)
}

// Deliberately trivial - this proves the JNI_OnLoad -> dex-load -> ComposeView chain renders REAL
// Compose (not a static View), not that Konative ships a real UI kit. A colored, non-default
// background makes "did Compose actually run" screenshot-verifiable at a glance. Uses foundation's
// BasicText, not material3's Text/MaterialTheme - a full Material3 design system (dynamic color,
// ripple, typography scale) is a lot of embedded-blob weight this trivial proof doesn't need; see
// embedded_kotlin/README.md for the measured size cost of adding it back.
//
// The second line (tickCountDisplay) is the one real addition beyond "prove Compose renders":
// reading a androidx.compose.runtime.State's .value (via the `by` delegate here) inside a
// composable automatically subscribes it to recomposition on change, so this line updates live,
// every real frame, driven by real C++-side state (KonativeAndroidApp::tick_count_ in
// jni_onload.cpp) - not just static text. Closes the loop from "the C++ ECS/systems core ticks,
// but only logcat ever sees it" to something actually visible on screen, still using only the
// same trivial foundation-only Compose surface as the line above it.
//
// The outer Box's Modifier.pointerInput block is the real producer for events/input/
// Touch{Down,Move,Up}Event.hpp (jni_onload.cpp's own on_started() comment has the full writeup).
// awaitPointerEventScope + a raw awaitPointerEvent() loop is the low-level Compose input API - this
// observes every real pointer change (down/move/up, any number of simultaneous pointers) without
// calling change.consume(), deliberately: this is a passive relay into the C++ event Dispatcher, not
// a gesture handler competing for input with some other UI element (there isn't one here yet).
// WindowFocusChangedEvent's real producer: LocalWindowInfo.current.isWindowFocused is a real
// Compose snapshot-State-backed value (androidx.compose.ui.platform.WindowInfo) - reading it here
// and keying a LaunchedEffect on it is the idiomatic Compose way to react to it changing, without
// needing to subclass ComposeView (which is `final` - confirmed via javap before attempting this;
// View.onWindowFocusChanged()/ViewTreeObserver would have been the fallback had LocalWindowInfo not
// existed in this vendored Compose version).
//
// KeyEvent's real producer: Modifier.onKeyEvent, which (unlike pointer input) is FOCUS-based, not
// position-based - the root Box must be made focusable (Modifier.focusable()) and actually hold
// focus, so a FocusRequester + a one-shot LaunchedEffect(Unit) { requestFocus() } grabs it on first
// composition (there's no visible click-to-focus affordance in this trivial UI to do it
// implicitly). Observes volume up/down specifically - real hardware keys present on every real
// Android device, unlike relying on a soft-keyboard/text-field trigger this UI has no reason to
// have (see jni_onload.cpp's own on_started() comment for the full reasoning). Does not call
// change.consume() equivalent (returns false) - same passive-relay philosophy as the pointer input
// block below: volume keys still do their normal system action (adjust media volume) too.
@Composable
private fun KonativeRootComposable() {
    val windowInfo = LocalWindowInfo.current
    LaunchedEffect(windowInfo.isWindowFocused) {
        dispatchWindowFocusChangedToNative(windowInfo.isWindowFocused)
    }
    val keyFocusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        keyFocusRequester.requestFocus()
    }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF1B5E20))
            .onSizeChanged { size -> dispatchWindowResizedToNative(size.width, size.height) }
            .focusRequester(keyFocusRequester)
            .focusable()
            .onKeyEvent { keyEvent ->
                // keyEvent.key.keyCode (a Long) is NOT the raw Android keycode in its low bits -
                // real on-device logging caught this: Compose packs it as (rawKeyCode shl 32), so
                // pressing KEYCODE_VOLUME_UP (24) produces key.keyCode == 103079215104
                // (24L shl 32), and a naive .toInt() truncates to the all-zero low 32 bits, always
                // reporting 0 regardless of which key was pressed. keyEvent.nativeKeyEvent (the real
                // underlying android.view.KeyEvent) exposes the correct raw Int directly - simpler
                // and less error-prone than un-packing key.keyCode's high bits by hand.
                val keyCode = keyEvent.nativeKeyEvent.keyCode
                when (keyEvent.type) {
                    KeyEventType.KeyDown -> dispatchKeyEventToNative(keyCode, true)
                    KeyEventType.KeyUp -> dispatchKeyEventToNative(keyCode, false)
                }
                false
            }
            .pointerInput(Unit) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        for (change in event.changes) {
                            val pointerId = change.id.value.toInt()
                            val x = change.position.x
                            val y = change.position.y
                            when {
                                change.changedToDown() -> dispatchTouchDownToNative(pointerId, x, y)
                                change.changedToUp() -> dispatchTouchUpToNative(pointerId, x, y)
                                change.positionChanged() -> dispatchTouchMoveToNative(pointerId, x, y)
                            }
                        }
                        touchCountDisplay = queryTouchCountFromNative()
                    }
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            BasicText(
                text = "Konative",
                style = TextStyle(color = Color.White, fontSize = 24.sp),
            )
            BasicText(
                text = "C++ ticks: $tickCountDisplay",
                style = TextStyle(color = Color.White, fontSize = 14.sp),
            )
            BasicText(
                text = "C++ touches: $touchCountDisplay",
                style = TextStyle(color = Color.White, fontSize = 14.sp),
            )
        }
    }
}
