// The real native entry point for the current design (ARCHITECTURE.md section 6.4) - replaces the
// deleted android_main.cpp/activity_bridge.cpp/looper_pump.cpp (android_native_app_glue/
// NativeActivity, section 6.1, superseded). System.loadLibrary() in testapp/'s MainActivity.kt
// triggers this automatically; there is no other native entry point for the app target anymore.

#include <cstddef>
#include <cstdint>
#include <span>

#include <jni.h>

#include "konative/core/log.hpp"
#include "konative/embed/checked_blob.hpp"
#include "konative/jni/dex_loader.hpp"

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

} // namespace

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
