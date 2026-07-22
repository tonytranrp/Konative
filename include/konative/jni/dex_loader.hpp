#pragma once

#include <cstddef>
#include <jni.h>

#include "konative/core/result.hpp"
#include "konative/jni/call.hpp"
#include "konative/jni/ref.hpp"

// Loads a class from an embedded DEX byte blob - the "how do we get from a native .so with only a
// JavaVM* to a live, callable JVM class with zero Java/Kotlin code anywhere else in the process"
// bridge (ARCHITECTURE.md section 6.4/6.6). Ported from GameHub's real, working
// libs/jni/src/dex_loader.cpp, adapted to Konative's own Result<T,E> convention instead of a
// falsy-struct-on-failure return, and to be fully inline (header-only wherever possible).
//
// Two distinct mechanisms combine here, each independently standard, not novel to this project:
//
//   1. Getting AN Android Context at all, starting from nothing but a JavaVM*:
//      android.app.ActivityThread.currentApplication() is a real, if unofficial (not public SDK
//      surface), static method the Android framework's OWN internals rely on to get the single
//      per-process Application instance from anywhere. Directly verified against Google's own
//      published hiddenapi-flags.csv to be in the permissive "unsupported" tier, never
//      max-target-X/blocked, continuously from API 29 through the current release
//      (research/jni_activity_bootstrap_research.md section 1.2) - the ONE hidden-API call this
//      loader makes; everything else below is ordinary public JNI/Android SDK usage.
//   2. Loading a class from bytes embedded in a native .so, without ever writing anything to the
//      host process's own files: dalvik.system.InMemoryDexClassLoader (API 26+) takes a direct
//      ByteBuffer wrapping already-in-memory DEX bytes. env->FindClass() from native code can't
//      see classes loaded by a non-default ClassLoader the normal way (it resolves relative to the
//      ClassLoader associated with the CALLING native method's own class, which doesn't apply here
//      since this isn't being called from Java) - the standard workaround, used here, is calling
//      the ClassLoader instance's own loadClass(String) method via ordinary reflection instead.
namespace konative::jni {

enum class DexLoadError {
    ActivityThreadClassNotFound,
    CurrentApplicationNull, // legitimately happens if called too early in process bring-up
    ClassLoaderNull,
    ByteBufferCreationFailed,
    DexClassLoaderConstructionFailed,
    ClassNotFoundInDex,
    GlobalRefPromotionFailed,
};

struct LoadedDexClass {
    GlobalRef<jclass> clazz;
    GlobalRef<jobject> application;
};

namespace detail {

// Opportunistically upgrades to a resource-aware effective loader if the embedded dex defines
// com.konative.generated.KonativeResourceProvider (see that Kotlin class's own doc comment for
// the full architectural reasoning - dalvik.system.InMemoryDexClassLoader is a FINAL class, so it
// cannot be subclassed to add resource support directly; KonativeResourceProvider is instead a
// plain java.lang.ClassLoader used as a SECOND InMemoryDexClassLoader's PARENT, relying on
// ClassLoader.getResource()'s standard parent-first delegation). Deliberately quiet on the
// expected "this dex doesn't define it" path (a plain ClassNotFoundException) - only
// construction failures on a provider class that DOES exist are real, loggable problems. Returns
// bootstrap_loader itself, unchanged, if no upgrade happened - never fails, never returns null.
inline LocalRef<jobject> upgrade_to_resource_aware_loader(JNIEnv* env, jobject bootstrap_loader,
                                                            jclass dex_loader_class, jmethodID dex_loader_ctor,
                                                            jobject dex_buffer_for_upgrade,
                                                            jobject real_parent_loader) {
    LocalRef<jstring> provider_name(
        env, env->NewStringUTF("com.konative.generated.KonativeResourceProvider"));
    if (check_and_clear_exception(env, "NewStringUTF(KonativeResourceProvider)") || !provider_name) {
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }

    LocalRef<jclass> loader_class(env, env->GetObjectClass(bootstrap_loader));
    jmethodID load_class_method =
        env->GetMethodID(loader_class.get(), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    loader_class.reset();
    LocalRef<jobject> provider_class(
        env, env->CallObjectMethod(bootstrap_loader, load_class_method, provider_name.get()));
    if (env->ExceptionCheck()) {
        // Expected, quiet path: this dex simply doesn't define the provider (a plain
        // ClassNotFoundException) - not an error, don't log it as one.
        env->ExceptionClear();
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }
    if (!provider_class) {
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }

    // Found it - construct KonativeResourceProvider(realParentLoader), then a SECOND
    // InMemoryDexClassLoader using that provider as ITS parent (not the real app classloader
    // directly), so classes loaded through this second loader see the provider first via
    // standard parent-first getResource() delegation.
    jmethodID provider_ctor = env->GetMethodID(static_cast<jclass>(provider_class.get()), "<init>",
                                                 "(Ljava/lang/ClassLoader;)V");
    if (check_and_clear_exception(env, "GetMethodID(KonativeResourceProvider.<init>)") ||
        provider_ctor == nullptr) {
        core::log_error("konative::jni::upgrade_to_resource_aware_loader: KonativeResourceProvider "
                         "found but has no (ClassLoader) constructor - using the plain loader "
                         "instead");
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }
    LocalRef<jobject> provider_instance(
        env, env->NewObject(static_cast<jclass>(provider_class.get()), provider_ctor, real_parent_loader));
    if (check_and_clear_exception(env, "NewObject(KonativeResourceProvider)") || !provider_instance) {
        core::log_error("konative::jni::upgrade_to_resource_aware_loader: KonativeResourceProvider "
                         "construction failed - using the plain loader instead");
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }
    LocalRef<jobject> resource_aware_loader(
        env, env->NewObject(dex_loader_class, dex_loader_ctor, dex_buffer_for_upgrade, provider_instance.get()));
    if (check_and_clear_exception(env, "NewObject(InMemoryDexClassLoader, resource-aware)") ||
        !resource_aware_loader) {
        core::log_error("konative::jni::upgrade_to_resource_aware_loader: second "
                         "InMemoryDexClassLoader construction failed - using the plain loader "
                         "instead");
        return LocalRef<jobject>(env, env->NewLocalRef(bootstrap_loader));
    }
    core::log_info("konative::jni::upgrade_to_resource_aware_loader: upgraded successfully");
    return resource_aware_loader;
}

} // namespace detail

inline core::Result<LoadedDexClass, DexLoadError> load_class_from_dex(
    JNIEnv* env, const unsigned char* dex_bytes, std::size_t dex_size,
    const char* fully_qualified_class_name) {
    using ErrResult = core::Result<LoadedDexClass, DexLoadError>;

    // Step 1: android.app.ActivityThread.currentApplication().
    LocalRef<jclass> activity_thread_class(env, env->FindClass("android/app/ActivityThread"));
    if (check_and_clear_exception(env, "FindClass(ActivityThread)") || !activity_thread_class) {
        return ErrResult::err(DexLoadError::ActivityThreadClassNotFound);
    }
    LocalRef<jobject> application(
        env, call_static_method<jobject>(env, activity_thread_class.get(), "currentApplication",
                                          "()Landroid/app/Application;"));
    if (!application) {
        core::log_error("konative::jni::load_class_from_dex: currentApplication() returned null "
                         "- too early in process startup?");
        return ErrResult::err(DexLoadError::CurrentApplicationNull);
    }

    // Step 2: Application.getClassLoader() - becomes InMemoryDexClassLoader's PARENT, so classes
    // the embedded DEX references (android.* framework classes) resolve normally through it.
    LocalRef<jobject> parent_loader(
        env, call_instance_method<jobject>(env, application.get(), "getClassLoader",
                                            "()Ljava/lang/ClassLoader;"));
    if (!parent_loader) {
        return ErrResult::err(DexLoadError::ClassLoaderNull);
    }

    // Step 3: wrap the embedded DEX bytes in a direct ByteBuffer and construct a
    // dalvik.system.InMemoryDexClassLoader from it - the bytes stay exactly where they already are
    // (linked, read-only data inside the caller's own .so, per KonativeEmbedBlob.cmake), no temp
    // file, no write anywhere in the host process's own storage.
    LocalRef<jobject> dex_buffer(env, env->NewDirectByteBuffer(const_cast<unsigned char*>(dex_bytes),
                                                                 static_cast<jlong>(dex_size)));
    if (check_and_clear_exception(env, "NewDirectByteBuffer") || !dex_buffer) {
        return ErrResult::err(DexLoadError::ByteBufferCreationFailed);
    }
    LocalRef<jclass> dex_loader_class(env, env->FindClass("dalvik/system/InMemoryDexClassLoader"));
    if (check_and_clear_exception(env, "FindClass(InMemoryDexClassLoader)") || !dex_loader_class) {
        return ErrResult::err(DexLoadError::DexClassLoaderConstructionFailed);
    }
    jmethodID ctor = env->GetMethodID(dex_loader_class.get(), "<init>",
                                       "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (check_and_clear_exception(env, "GetMethodID(InMemoryDexClassLoader.<init>)") ||
        ctor == nullptr) {
        return ErrResult::err(DexLoadError::DexClassLoaderConstructionFailed);
    }
    LocalRef<jobject> dex_class_loader(
        env, env->NewObject(dex_loader_class.get(), ctor, dex_buffer.get(), parent_loader.get()));
    if (check_and_clear_exception(env, "NewObject(InMemoryDexClassLoader)") || !dex_class_loader) {
        return ErrResult::err(DexLoadError::DexClassLoaderConstructionFailed);
    }

    // Step 3.5: opportunistically upgrade to a resource-aware effective loader if the embedded dex
    // defines com.konative.generated.KonativeResourceProvider (see
    // detail::upgrade_to_resource_aware_loader's own doc comment) - gives real
    // java.util.ServiceLoader-dependent libraries (kotlinx-coroutines' Main dispatcher being the
    // concrete, reproduced case; ARCHITECTURE.md section 6.6) a working
    // getResource()/getResourceAsStream() even though InMemoryDexClassLoader itself never
    // provides one. A second, independent direct ByteBuffer over the same bytes - NOT dex_buffer
    // reused - since dex_buffer is already associated with dex_class_loader.
    LocalRef<jobject> dex_buffer_for_upgrade(
        env, env->NewDirectByteBuffer(const_cast<unsigned char*>(dex_bytes), static_cast<jlong>(dex_size)));
    if (check_and_clear_exception(env, "NewDirectByteBuffer(for upgrade)") || !dex_buffer_for_upgrade) {
        return ErrResult::err(DexLoadError::ByteBufferCreationFailed);
    }
    LocalRef<jobject> effective_loader = detail::upgrade_to_resource_aware_loader(
        env, dex_class_loader.get(), dex_loader_class.get(), ctor, dex_buffer_for_upgrade.get(),
        parent_loader.get());

    // Step 4: ClassLoader.loadClass(String) via reflection, NOT env->FindClass() - see this file's
    // own top comment for why FindClass can't see this class. loadClass wants Java source-style
    // dotted names, unlike FindClass's slash-separated internal names.
    LocalRef<jstring> class_name(env, env->NewStringUTF(fully_qualified_class_name));
    if (check_and_clear_exception(env, "NewStringUTF") || !class_name) {
        return ErrResult::err(DexLoadError::ClassNotFoundInDex);
    }
    LocalRef<jobject> loaded_obj(
        env, call_instance_method<jobject>(env, effective_loader.get(), "loadClass",
                                            "(Ljava/lang/String;)Ljava/lang/Class;", class_name.get()));
    if (!loaded_obj) {
        core::log_error("konative::jni::load_class_from_dex: loadClass failed for {}",
                         fully_qualified_class_name);
        return ErrResult::err(DexLoadError::ClassNotFoundInDex);
    }

    // Promote both to GLOBAL refs - every LocalRef<T> acquired above releases itself automatically
    // as this function returns, no manual DeleteLocalRef anywhere in this function.
    LoadedDexClass result;
    result.clazz = GlobalRef<jclass>(env, static_cast<jclass>(loaded_obj.get()));
    result.application = GlobalRef<jobject>(env, application.get());
    if (!result.clazz || !result.application) {
        check_and_clear_exception(env, "NewGlobalRef");
        result.clazz.reset(env);
        result.application.reset(env);
        return ErrResult::err(DexLoadError::GlobalRefPromotionFailed);
    }
    return ErrResult::ok(std::move(result));
}

} // namespace konative::jni
