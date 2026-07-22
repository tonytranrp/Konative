package com.konative.generated

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import android.util.Log
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
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

            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
                if (owner != null) return // only the first Activity matters for this entry point
                owner = ComposeHostOwner().apply { performRestore(savedInstanceState) }
                owner!!.registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)
                dispatchToNative(AndroidLifecycleEvent.STARTED)

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
            }

            override fun onActivityPaused(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)
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
}

// Mirrors jni_onload.cpp's own AndroidLifecycleEvent enum exactly - both sides must stay in sync.
private object AndroidLifecycleEvent {
    const val STARTED = 0
    const val RESUMED = 1
    const val PAUSED = 2
    const val DESTROYED = 3
}

// Wraps every real nativeDispatchLifecycle() call site: if RegisterNatives ever failed to bind this
// method (jni_onload.cpp's own JNI_OnLoad logs why, non-fatally, if so), an uncaught
// UnsatisfiedLinkError here would crash the Activity's own onResume()/onPause()/onDestroy() -
// breaking real Compose rendering over a C++-side feature that's supposed to degrade gracefully.
// Logs once per occurrence rather than silently swallowing it, matching this codebase's own
// "the self-check should report clearly, not silently swallow a failure" standing rule
// (embedded_kotlin/README.md, core/log.hpp's own doc comment).
private fun dispatchToNative(event: Int) {
    try {
        KonativeEntryPoint.nativeDispatchLifecycle(event)
    } catch (e: UnsatisfiedLinkError) {
        Log.e("Konative", "dispatchToNative: nativeDispatchLifecycle unavailable (event=$event) - " +
            "the C++ ECS/events core will not see this transition; Compose is unaffected.", e)
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
@Composable
private fun KonativeRootComposable() {
    Box(
        modifier = Modifier.fillMaxSize().background(Color(0xFF1B5E20)),
        contentAlignment = Alignment.Center,
    ) {
        BasicText(
            text = "Konative",
            style = TextStyle(color = Color.White, fontSize = 24.sp),
        )
    }
}
