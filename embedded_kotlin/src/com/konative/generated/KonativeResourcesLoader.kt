package com.konative.generated

import android.app.Activity
import android.content.res.loader.AssetsProvider
import android.content.res.loader.ResourcesLoader
import android.content.res.loader.ResourcesProvider
import android.os.Build
import android.os.ParcelFileDescriptor
import android.system.Os
import android.system.OsConstants
import android.util.Log
import java.nio.ByteBuffer

// The general, comprehensive fix for the resources.arsc runtime gap embedded_kotlin/README.md's
// earlier updates document in full - unlike KonativeResourceStringOverride.kt's small, scoped patch
// (2 hardcoded, English-only fields), this loads the REAL, complete, correctly-localized
// resources.arsc that KonativeCompileKotlinDex.cmake's Step 1.5 already produces (via `aapt2 link`)
// and now keeps instead of discarding - see that file's own comment for exactly where the bytes
// come from and how they're embedded (a second .incbin blob, sibling to the dex blob, extracted at
// build time via KonativeEmbedKotlinDex.cmake's second konative_embed_binary_blob() call).
//
// Real, public, non-reflective Android API (android.content.res.loader.*, API 30+ / Android 11) -
// confirmed via javap against real, local API-30 and API-36 android.jar stubs before writing this,
// not assumed. Requires API 30+; KONATIVE_MIN_API is 26, so this whole mechanism is skipped below
// that floor - KonativeResourceStringOverride.kt's scoped patch is the fallback there (and whenever
// this function returns false for any other reason). See
// installResourcesAndGetComposeContext() in KonativeEntryPoint.kt for the real selection logic
// between the two.
//
// Real mechanism: android.system.Os.memfd_create() gives an anonymous, tmpfs-backed file
// descriptor with no real filesystem path - the closest public-API equivalent to "construct
// straight from an in-memory buffer," philosophically parallel to how the dex itself is loaded via
// InMemoryDexClassLoader without ever touching disk. Write the embedded bytes into it, wrap it as a
// ParcelFileDescriptor, hand that to ResourcesProvider.loadFromTable(), add the resulting provider
// to a ResourcesLoader, then attach that loader directly to the real host Activity's OWN Resources
// instance (activity.resources.addLoaders(...)) - this MUTATES that instance in place, so (unlike
// the scoped patch) no Context/Resources wrapping is needed here at all: whatever Context
// ComposeView(...) later receives, as long as its getResources() ultimately returns this same
// Activity Resources object (true for the real, unwrapped Activity), sees the added table
// automatically the next time anything calls getString()/etc. on it.
//
// object : AssetsProvider {} (a trivial instance satisfying the interface's one `default` method)
// is passed explicitly rather than null, to avoid depending on whether loadFromTable() tolerates a
// null AssetsProvider - a real ambiguity in the public API surface (stub jars strip method bodies,
// so this can't be settled by decompilation alone) this sidesteps entirely rather than guesses at.
fun tryInstallGeneralResourcesLoader(activity: Activity, resourcesArscBuffer: ByteBuffer?): Boolean {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
        Log.i(
            "Konative",
            "tryInstallGeneralResourcesLoader: API ${Build.VERSION.SDK_INT} < 30 - " +
                "ResourcesLoader isn't available, falling back to the scoped string override.",
        )
        return false
    }
    if (resourcesArscBuffer == null) {
        Log.w(
            "Konative",
            "tryInstallGeneralResourcesLoader: no resources.arsc blob was embedded (self-check " +
                "failed at JNI_OnLoad?) - falling back to the scoped string override.",
        )
        return false
    }

    return try {
        val bytes = ByteArray(resourcesArscBuffer.remaining())
        resourcesArscBuffer.get(bytes)

        val fd = Os.memfd_create("konative_resources", 0)
        // loader is hoisted out of the inner try so the self-check failure branch below can still
        // reach it to undo addLoaders() - see that branch's own comment for why.
        val loader = try {
            Os.write(fd, bytes, 0, bytes.size)
            // Rewind before handing off - Os.write() leaves the fd's own position at the end of
            // what was just written, and loadFromTable() reads from the fd's current position.
            Os.lseek(fd, 0, OsConstants.SEEK_SET)
            val pfd = ParcelFileDescriptor.dup(fd)
            val provider = ResourcesProvider.loadFromTable(pfd, object : AssetsProvider {})
            val newLoader = ResourcesLoader()
            newLoader.addProvider(provider)
            activity.resources.addLoaders(newLoader)
            newLoader
        } finally {
            // dup() above creates an independent fd inside the ParcelFileDescriptor - this
            // original memfd is no longer needed once that succeeds (or once we're bailing out via
            // the catch block below).
            Os.close(fd)
        }

        // Real, permanent self-check (matches this framework's own "code checks itself" standing
        // design principle) - confirms Resources.getString() on one of the same fields
        // (the "Tab" role description) KonativeResourceStringOverride.kt's own self-check covers
        // now genuinely resolves through the real table, not just that addLoaders() didn't throw.
        // Only checks this one field, not also "Switch" - the whole blob was already SHA-256
        // -verified byte-for-byte before Kotlin ever saw it (jni_onload.cpp), so a scenario where
        // the table loads and this field resolves while a sibling field in the same table doesn't
        // is not a realistic case worth separately guarding against. Deliberately does NOT assert
        // an exact string value here (unlike that other self-check) - the whole point of this
        // mechanism is real, correctly-localized values, which legitimately vary by device locale;
        // asserting a specific English string would incorrectly flag success as failure.
        val verifyTab = runCatching { activity.resources.getString(0x7f08001b) }
        if (verifyTab.isFailure) {
            // Real cleanup, not just a failure report: addLoaders() above already durably mutated
            // the real Activity Resources/AssetManager - callers (KonativeEntryPoint.kt) treat this
            // function's return value as a clean either/or against KonativeResourceStringOverride's
            // scoped patch, but without this, a table that loads successfully yet fails this
            // specific field lookup (e.g. a future AAPT2 relink reassigning this id) would leave
            // BOTH mechanisms layered on the same AssetManager instead of either/or - a real gap a
            // 2026-07-22 code-review pass found was never actually exercised in testing (this
            // file's own README note only ever forced an early return before addLoaders() ran, not
            // this genuine post-mutation failure path).
            activity.resources.removeLoaders(loader)
            Log.e(
                "Konative",
                "tryInstallGeneralResourcesLoader: addLoaders() succeeded but " +
                    "Resources.getString() still fails for a known field (${verifyTab.exceptionOrNull()}) " +
                    "- removed the loader and falling back to the scoped string override instead.",
            )
            return false
        }

        Log.i(
            "Konative",
            "tryInstallGeneralResourcesLoader: real resources.arsc loaded and verified " +
                "successfully (API ${Build.VERSION.SDK_INT}).",
        )
        true
    } catch (e: Exception) {
        Log.e(
            "Konative",
            "tryInstallGeneralResourcesLoader: failed to install the real resources table " +
                "(API ${Build.VERSION.SDK_INT}) - falling back to the scoped string override.",
            e,
        )
        false
    }
}
