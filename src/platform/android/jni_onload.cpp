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

    // Step 6: ONE handoff call, then native code is done - everything past this point (Compose
    // wiring, ActivityLifecycleCallbacks) is real, compiled Kotlin (ARCHITECTURE.md section 6.4).
    jmethodID install = env->GetStaticMethodID(loaded.value().clazz.get(), "install",
                                                 "(Landroid/app/Application;)V");
    if (konative::jni::check_and_clear_exception(env, "GetStaticMethodID(install)") ||
        install == nullptr) {
        konative::core::log_error("JNI_OnLoad: {} has no install(Application) static method",
                                   kEntryPointClass);
        return JNI_VERSION_1_6;
    }
    env->CallStaticVoidMethod(loaded.value().clazz.get(), install, loaded.value().application.get());
    konative::jni::check_and_clear_exception(env, "CallStaticVoidMethod(install)");

    return JNI_VERSION_1_6;
}
