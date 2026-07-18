package com.konative.generated

import java.io.ByteArrayInputStream
import java.io.InputStream
import java.net.URL
import java.net.URLConnection
import java.net.URLStreamHandler
import java.util.Collections
import java.util.Enumeration

// The real fix for the architectural gap embedded_kotlin/README.md's Status section documents:
// dalvik.system.InMemoryDexClassLoader loads bytecode from a raw byte buffer with NO JAR/ZIP
// resource backing at all, so ClassLoader.getResource()/getResourceAsStream()/getResources()
// always return null/empty/nothing for any class loaded this way - which breaks any library
// relying on META-INF/services/*-based java.util.ServiceLoader discovery. kotlinx-coroutines'
// Main-dispatcher discovery is the concrete, reproduced case that blocked Compose from rendering;
// there may be others. Only the plain java.util.ServiceLoader-style fallback path actually needs
// this resource lookup - a later, independent verification pass decompiled
// kotlinx.coroutines.internal.FastServiceLoader further than the original diagnosis had and found
// that on a real Android device, the "fast" path kotlinx-coroutines actually takes uses a
// hardcoded Class.forName(...) lookup instead, needing no resources at all (see r8-rules.pro's own
// kotlinx.coroutines.android.** keep rule for the separate, real problem that path DOES have -
// R8 shrinking the looked-up class away entirely).
//
// NOT an InMemoryDexClassLoader subclass - dalvik.system.InMemoryDexClassLoader is a final class
// (a real, compile-time-verified constraint, not a style choice - an earlier draft tried
// subclassing it directly and got "this type is final, so it cannot be extended"). Instead, this
// is a plain ClassLoader used as InMemoryDexClassLoader's PARENT argument (not a delegating
// wrapper sitting beside it): java.lang.ClassLoader.getResource()/getResourceAsStream()'s
// DEFAULT, UN-overridden implementation delegates to the parent FIRST, only falling back to the
// loader's own findResource() if the parent returns null - so any class loaded by the real
// InMemoryDexClassLoader (which never overrides getResource() itself, only findResource())
// automatically checks this parent first when something calls .getResource() on it, without this
// class needing to touch class-loading at all.
//
// src/platform/android/jni_onload.cpp's load_class_from_dex() bootstraps this: it first
// constructs a PLAIN InMemoryDexClassLoader (parent = the real Application classloader) to try
// loading this exact class by name; only if found, it constructs an INSTANCE of this class
// (parent = the real Application classloader) and then constructs a SECOND
// InMemoryDexClassLoader using THIS instance as ITS parent instead, and loads the real entry
// point through that one. A dex blob that doesn't define this class (e.g. the simpler placeholder
// used to verify the base JNI_OnLoad chain, ARCHITECTURE.md section 6.7) still works unchanged -
// this mechanism is opportunistic, not required.
class KonativeResourceProvider(parent: ClassLoader) : ClassLoader(parent) {

    override fun findResource(name: String): URL? {
        val content = SYNTHETIC_RESOURCES[name] ?: return null
        return URL(null, "konative-synthetic-resource:$name", SyntheticStreamHandler(content))
    }

    override fun findResources(name: String): Enumeration<URL> {
        val resource = findResource(name) ?: return Collections.emptyEnumeration()
        return Collections.enumeration(listOf(resource))
    }

    // URL requires SOME stream handler to know how to open a connection for a non-standard
    // protocol ("konative-synthetic-resource:") - passing one directly to the URL constructor
    // (used above) is the standard, documented way to do this without the global, process-wide
    // URL.setURLStreamHandlerFactory() registration, which this framework has no business doing
    // (it would affect every URL in the whole host process, not just this loader's own lookups).
    //
    // Constructor parameter named resourceBytes, NOT content - java.net.URLConnection itself
    // declares a real getContent(): Object method, which Kotlin's Java interop exposes as an
    // inherited property named `content` (type Any!) - inside the anonymous URLConnection
    // subclass below, an unqualified `content` resolves to THAT inherited property, silently
    // shadowing this class's own constructor parameter of the same name, not a typo. Caught by
    // an actual compile ("actual type is 'Any!', but 'ByteArray!' was expected"), not by review.
    private class SyntheticStreamHandler(private val resourceBytes: ByteArray) : URLStreamHandler() {
        override fun openConnection(url: URL): URLConnection = object : URLConnection(url) {
            override fun connect() {}
            override fun getInputStream(): InputStream = ByteArrayInputStream(resourceBytes)
        }
    }

    companion object {
        // Real ServiceLoader registration file CONTENT, copied verbatim from the real
        // kotlinx-coroutines-android AAR's own META-INF/services/ entries (confirmed via
        // `unzip -p kotlinx-coroutines-android-*.jar META-INF/services/...`, not guessed). The
        // class names these files list are real, published kotlinx-coroutines-android API.
        private val SYNTHETIC_RESOURCES: Map<String, ByteArray> = mapOf(
            "META-INF/services/kotlinx.coroutines.internal.MainDispatcherFactory" to
                "kotlinx.coroutines.android.AndroidDispatcherFactory\n".toByteArray(Charsets.UTF_8),
            "META-INF/services/kotlinx.coroutines.CoroutineExceptionHandler" to
                "kotlinx.coroutines.android.AndroidExceptionPreHandler\n".toByteArray(Charsets.UTF_8),
        )
    }
}
