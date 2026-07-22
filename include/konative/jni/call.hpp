#pragma once

#include <jni.h>
#include <type_traits>
#include <utility>

#include "konative/core/log.hpp"
#include "konative/jni/ref.hpp"

// JNI call helpers - ported from GameHub's libs/jni/include/gamehub/jni/call.hpp (real, working
// code - see ARCHITECTURE.md section 6.6), adapted to be fully inline (Konative is header-only
// wherever possible - src/README.md's own hard rule) and to log via konative::core::log_error
// instead of GameHub's own logging module.
namespace konative::jni {

// Logs (via konative::core::log_error) and clears any exception currently pending on env. Returns
// true if one WAS pending. Exposed publicly, not just used internally by call_static_method/
// call_instance_method below, because a handful of real call sites (NewDirectByteBuffer,
// NewStringUTF, FindClass) are not method calls at all and still need this exact check: a pending
// exception must be cleared before making ANY further JNI call, including DeleteLocalRef/
// DeleteGlobalRef during unwind.
inline bool check_and_clear_exception(JNIEnv* env, const char* what) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        konative::core::log_error("JNI exception during {}", what);
        return true;
    }
    return false;
}

namespace detail {

// `return R{};` in a function template generic over R is not portable when R can be void -
// `return default_value<R>();` is the shape every compiler accepts.
template <typename R>
R default_value() {
    if constexpr (!std::is_same_v<R, void>) {
        return R{};
    }
}

template <typename R, typename... Args>
R invoke_static(JNIEnv* env, jclass clazz, jmethodID method, Args&&... args) {
    if constexpr (std::is_same_v<R, void>) {
        env->CallStaticVoidMethod(clazz, method, std::forward<Args>(args)...);
    } else if constexpr (std::is_convertible_v<R, jobject>) {
        return static_cast<R>(env->CallStaticObjectMethod(clazz, method, std::forward<Args>(args)...));
    } else if constexpr (std::is_same_v<R, jboolean>) {
        return env->CallStaticBooleanMethod(clazz, method, std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<R, jint>) {
        return env->CallStaticIntMethod(clazz, method, std::forward<Args>(args)...);
    }
    // Extend with jlong/jfloat/jdouble/... only once a real call site needs one.
}

template <typename R, typename... Args>
R invoke_instance(JNIEnv* env, jobject obj, jmethodID method, Args&&... args) {
    if constexpr (std::is_same_v<R, void>) {
        env->CallVoidMethod(obj, method, std::forward<Args>(args)...);
    } else if constexpr (std::is_convertible_v<R, jobject>) {
        return static_cast<R>(env->CallObjectMethod(obj, method, std::forward<Args>(args)...));
    } else if constexpr (std::is_same_v<R, jboolean>) {
        return env->CallBooleanMethod(obj, method, std::forward<Args>(args)...);
    } else if constexpr (std::is_same_v<R, jint>) {
        return env->CallIntMethod(obj, method, std::forward<Args>(args)...);
    }
}

} // namespace detail

// Looks up a STATIC method by name+signature and invokes it, checking/clearing any resulting
// exception - collapses the repeated "GetStaticMethodID + Call*Method + check-and-clear-exception"
// pattern into one call. Templated ONLY on the return type R - the signature string itself stays
// an explicit, human-visible, javap-diffable literal at every call site, deliberately not deduced
// from Args.
//
// Returns a default-constructed R (nullptr/0/false) if clazz is null, the method isn't found, or
// the call itself raises an exception.
template <typename R, typename... Args>
R call_static_method(JNIEnv* env, jclass clazz, const char* name, const char* signature, Args&&... args) {
    if (clazz == nullptr) {
        return detail::default_value<R>();
    }
    jmethodID method = env->GetStaticMethodID(clazz, name, signature);
    if (check_and_clear_exception(env, name) || method == nullptr) {
        return detail::default_value<R>();
    }
    if constexpr (std::is_same_v<R, void>) {
        detail::invoke_static<R>(env, clazz, method, std::forward<Args>(args)...);
        check_and_clear_exception(env, name);
    } else {
        R result = detail::invoke_static<R>(env, clazz, method, std::forward<Args>(args)...);
        check_and_clear_exception(env, name);
        return result;
    }
}

// Same shape, for an INSTANCE method (GetMethodID + Call<R>Method).
template <typename R, typename... Args>
R call_instance_method(JNIEnv* env, jobject obj, const char* name, const char* signature, Args&&... args) {
    if (obj == nullptr) {
        return detail::default_value<R>();
    }
    LocalRef<jclass> clazz(env, env->GetObjectClass(obj));
    if (check_and_clear_exception(env, name) || !clazz) {
        return detail::default_value<R>();
    }
    jmethodID method = env->GetMethodID(clazz.get(), name, signature);
    clazz.reset();
    if (check_and_clear_exception(env, name) || method == nullptr) {
        return detail::default_value<R>();
    }
    if constexpr (std::is_same_v<R, void>) {
        detail::invoke_instance<R>(env, obj, method, std::forward<Args>(args)...);
        check_and_clear_exception(env, name);
    } else {
        R result = detail::invoke_instance<R>(env, obj, method, std::forward<Args>(args)...);
        check_and_clear_exception(env, name);
        return result;
    }
}

} // namespace konative::jni
