# R8 rules for embedded_kotlin/ - part of the hand-run kotlinc+d8/r8 recipe documented in
# embedded_kotlin/README.md's Status section (real, verified working: a screenshot of actual
# rendered Compose output - a green box with white "Konative" text - exists as of the commit that
# added this file). Every rule below exists because a REAL on-device crash, decompiled bytecode
# reference, or measured build failure demanded it - see the comment on each rule for the exact
# evidence, not just "seemed safer." Once the real konative_embed_kotlin_dex()-style CMake
# automation exists (ARCHITECTURE.md section 6.6), it should invoke r8 with `--pg-conf` pointing
# at this file (in addition to the real AAR-bundled consumer rules R8 auto-discovers from
# META-INF/com.android.tools/r8/*.pro and META-INF/proguard/*.pro inside the dependency jars
# themselves - those are NOT duplicated here, R8 finds them automatically from its own inputs).

# --- Konative's own entry point - called only via JNI reflection (GetStaticMethodID +
# CallStaticVoidMethod from src/platform/android/jni_onload.cpp), invisible to R8's static call
# graph, so it must be kept explicitly or R8 treats it as dead code and strips it. ---
-keep class com.konative.generated.KonativeEntryPoint {
    public static void install(android.app.Application);
}
-keep class com.konative.generated.KonativeEntryPointKt { *; }

# Same story: dex_loader.hpp's upgrade_to_resource_aware_loader() loads this ONLY via
# bootstrap_loader.loadClass("com.konative.generated.KonativeResourceProvider") by string name -
# invisible to R8's static call graph exactly like KonativeEntryPoint.install() above. A real bug,
# not a hypothetical: confirmed via `strings classes.dex | grep KonativeResourceProvider` finding
# NOTHING before this rule was added - R8 had silently shrunk the whole class away, and
# upgrade_to_resource_aware_loader's own "class not found" path is deliberately silent (the
# expected case for a dex that doesn't opt into this mechanism at all), which is exactly why this
# was hard to diagnose - it looked identical to the intentional no-op fallback.
-keep class com.konative.generated.KonativeResourceProvider { *; }

# --- Optional/soft integrations Compose UI probes for defensively (emoji fallback font loading,
# foldable/window posture) - real apps that want them add the real androidx.emoji2/androidx.window
# dependencies; this trivial proof module intentionally doesn't, matching "don't add bloat" ---
-dontwarn androidx.emoji2.**
-dontwarn androidx.window.**

# --- androidx.core.content.res.FontResourcesParserCompat's XML custom-font-family parsing path
# needs androidx.core.R$styleable (real AAPT2-linked attribute arrays), which this hand-rolled
# pipeline has no AAPT2 step to generate. Not shimmed (unlike the R$id/R$string tag-key shims in
# embedded_kotlin/r_shim/) because a styleable int[] needs real, correctly-ordered attribute
# indices to be meaningful, not just present - a wrong shim here would be worse than an honest
# dontwarn. Safe to suppress: this trivial demo references no custom XML font-family resources, so
# this codepath is genuinely unreachable, not just unlikely. ---
-dontwarn androidx.core.R$styleable

# --- Kept for readable crash diagnostics, not because obfuscation itself caused a bug: an actual
# on-device crash (java.lang.NoSuchFieldError on a renamed one-letter class) was originally
# unreadable with default --release obfuscation on. Turning it off surfaced the real, readable
# field name, which turned out to be a genuinely missing embedded_kotlin/r_shim/ field (this
# hand-rolled pipeline has no real AAPT2 resource linking) - now added. Kept off going forward
# since this pipeline has no reason to obfuscate a not-yet-shipped proof-of-concept anyway. ---
-dontobfuscate

# --- Real, reproduced on-device crash: java.lang.NoSuchMethodError on kotlin.collections.ArraysKt
# .fill$default([J)V, thrown from androidx.collection.MutableScatterMap.initializeStorage/<init>.
# Reproduced identically against BOTH kotlin-stdlib 2.1.20 and 2.4.0 (ruling out a version
# mismatch), and reproduces with or without the resource-provider class present - genuinely an R8
# optimizer bug/mismatch with this specific bridge-method call shape, not investigated further.
# TODO for whoever builds the real CMake automation: bisect via fine-grained -optimizations flags
# rather than this blanket one, if the size savings from full optimization turn out to matter. ---
-dontoptimize

# --- kotlinx-coroutines-android's OWN bundled R8 rules (auto-applied by R8 from that library's own
# META-INF/com.android.tools/r8-from-1.6.0/coroutines.pro - an -assumenosideeffects block, not
# duplicated here) are written to work WITH R8's optimizer: they let R8 inline
# AndroidDispatcherFactory construction directly, skipping runtime reflection/ServiceLoader
# entirely (that file's own comment says so explicitly). With -dontoptimize set above, that
# inlining can't happen, and since NOTHING in this module's own code calls AndroidDispatcherFactory
# directly (only kotlinx.coroutines.internal.FastServiceLoader's
# Class.forName("kotlinx.coroutines.android.AndroidDispatcherFactory", ...) does - confirmed by
# decompiling FastServiceLoader.loadMainDispatcherFactory$kotlinx_coroutines_core() directly -
# invisible to R8's shrinker same as any other reflection-only reference), R8's shrinker removes it
# as apparently-unreachable. Reproduced on-device (java.lang.IllegalStateException: "Module with
# the Main dispatcher is missing") with a working KonativeResourceProvider already in place, so
# this was NOT a resource-lookup problem despite looking identical to one - keeping just
# AndroidDispatcherFactory itself was NOT enough either (it also needs its own internal
# dependencies, e.g. HandlerContext/HandlerDispatcherKt); the whole package is small (7 classes,
# confirmed via `unzip -l` on the real AAR jar), so keep all of it rather than chase individual
# missing internals one at a time. ---
-keep class kotlinx.coroutines.android.** { *; }
