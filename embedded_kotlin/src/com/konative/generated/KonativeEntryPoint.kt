package com.konative.generated

import android.app.Activity
import android.app.Application
import android.os.Bundle
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

// The one entry point src/platform/android/jni_onload.cpp's InMemoryDexClassLoader-loaded class
// resolves and calls (ARCHITECTURE.md section 6.4 step 3 / research/jni_activity_bootstrap_research.md
// section 5.2 - implemented against that design directly, not re-derived). Everything past
// JNI_OnLoad's one CallStaticVoidMethod handoff is real, compiled Kotlin from here on - no further
// JNI reflection, per this project's own "favor real Kotlin over reflection" rule.
object KonativeEntryPoint {
    @JvmStatic
    fun install(application: Application) {
        application.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            private var owner: ComposeHostOwner? = null

            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
                if (owner != null) return // only the first Activity matters for this entry point
                owner = ComposeHostOwner().apply { performRestore(savedInstanceState) }
                owner!!.registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)

                val composeView = ComposeView(activity).apply {
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
            }

            override fun onActivityPaused(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE)
            }

            override fun onActivityStopped(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_STOP)
            }

            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {
                owner?.performSave(outState)
            }

            override fun onActivityDestroyed(activity: Activity) {
                owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_DESTROY)
            }
        })
    }
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
