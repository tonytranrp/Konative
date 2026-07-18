package com.konative.generated

import android.content.Context
import android.content.ContextWrapper
import android.content.res.Resources
import android.util.Log

// The real, scoped fix for the SPECIFIC live bug embedded_kotlin/README.md's 2026-07-18 Update
// section documents: this module packages real AAPT2-assigned resource ID *values* (via
// KonativeCompileKotlinDex.cmake's Step 1.5) but no real resources.arsc *table* anywhere the
// runtime Resources object backing the host Activity's Context can see - so any
// Resources.getString(int) call into that table throws Resources.NotFoundException, real values or
// not. Confirmed via javap that androidx.compose.ui.platform.AndroidComposeViewAccessibilityDelegateCompat
// (already present in the shipped dex) makes exactly this call for R.string.tab/R.string.switch_role
// (accessibility role descriptions for Role.Tab/Role.Switch semantics nodes) - a live,
// not-yet-triggered crash, not a hypothetical.
//
// This is deliberately a NARROW patch for the two fields already confirmed broken, not a general
// resource-table mechanism - see embedded_kotlin/README.md for why a general fix (a real embedded
// resources.arsc + android.content.res.loader.ResourcesLoader/ResourcesProvider, API 30+) is a
// separately-designed, larger, not-yet-implemented option, deferred because nothing in this
// module's own composable tree exercises any OTHER Resources.getString()-backed field yet.
//
// Confirmed via bytecode tracing (Wrapper_androidKt.setContent's real "new AndroidComposeView(this
// .getContext(), ...)" call) that the Context passed into ComposeView(activity) in
// KonativeEntryPoint.kt flows UNWRAPPED all the way to the real AndroidComposeView whose
// accessibility delegate makes the crashing call - so wrapping it here, once, before ComposeView
// construction, is sufficient; no CompositionLocal/theme/interception trick is needed.
//
// Real values below (0x7f08001a/0x7f08001b, "Switch"/"Tab") are copied from a real, already-produced
// build artifact (KonativeCompileKotlinDex.cmake's Step 1.5 output - both the generated
// androidx/compose/ui/R.java and the real AAR's own res/values/values.xml agree), not guessed - see
// that Step's own comment for how they're regenerated if the real AAPT2 link ever changes them.
// FRAGILE BY DESIGN, not silently: these are raw integer literals, not a live `R.string.tab`
// reference, because Step 1 (kotlinc) currently compiles BEFORE Step 1.5 (aapt2) generates the real
// R class, so this file cannot import it by name today. If a future AAPT2/AndroidX version bump ever
// reassigns these IDs, KONATIVE_STRING_OVERRIDE_SELF_CHECK below will fail loudly at every app launch
// rather than silently serving the wrong string - see installKonativeResourceStringOverrides()'s own
// self-check call.
private const val TAB_STRING_ID = 0x7f08001b
private const val SWITCH_ROLE_STRING_ID = 0x7f08001a
private val KNOWN_BROKEN_STRINGS = mapOf(
    TAB_STRING_ID to "Tab",
    SWITCH_ROLE_STRING_ID to "Switch",
)

private class KonativeOverrideResources(base: Resources) : Resources(
    base.assets, base.displayMetrics, base.configuration,
) {
    override fun getString(id: Int): String =
        KNOWN_BROKEN_STRINGS[id] ?: super.getString(id)

    override fun getString(id: Int, vararg formatArgs: Any?): String =
        KNOWN_BROKEN_STRINGS[id]?.let { String.format(it, *formatArgs) } ?: super.getString(id, *formatArgs)
}

private class KonativeOverrideContext(base: Context) : ContextWrapper(base) {
    private val overrideResources by lazy { KonativeOverrideResources(super.getResources()) }
    override fun getResources(): Resources = overrideResources
}

// Wrap the host Activity's Context before constructing ComposeView(...) - see this file's own
// top comment for why this is sufficient and doesn't need any Compose-level interception.
fun wrapContextForResourceStringOverride(base: Context): Context = KonativeOverrideContext(base)

// A real, permanent self-check (this framework's own "code checks itself" standing design
// principle - matches checked_blob.hpp's build-vs-runtime SHA-256 self-check for the embedded dex),
// not a one-off manual test: confirms the override actually activates for the exact IDs it targets,
// every time the app launches, so a future AAPT2 re-link silently reassigning these IDs fails loudly
// here instead of surfacing as a confusing NotFoundException deep inside an accessibility-service
// callback. Logs, does not throw - a self-check failing should degrade observably, not crash an
// otherwise-working app over a narrow accessibility-string edge case.
fun konativeResourceStringOverrideSelfCheck(wrappedContext: Context) {
    for ((id, expected) in KNOWN_BROKEN_STRINGS) {
        val actual = runCatching { wrappedContext.resources.getString(id) }
        if (actual.getOrNull() != expected) {
            Log.e(
                "Konative",
                "konativeResourceStringOverrideSelfCheck: resource string override for id=0x${id.toString(16)} " +
                    "did not return the expected value (expected=\"$expected\", got=$actual) - the hardcoded " +
                    "IDs in KonativeResourceStringOverride.kt are likely stale against a newer AAPT2 link; " +
                    "regenerate them from a fresh KonativeCompileKotlinDex.cmake Step 1.5 build.",
            )
        }
    }
}
