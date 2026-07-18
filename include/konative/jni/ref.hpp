#pragma once

#include <cassert>
#include <jni.h>
#include <type_traits>

// RAII wrappers around JNI references - ported from GameHub's libs/jni/include/gamehub/jni/ref.hpp
// (real, working, on-device-proven code - see ARCHITECTURE.md section 6.6), not reinvented.
// Deliberately small: no weak-ref type, no JNI array/element wrapper - extend only when a real
// call site needs more.
namespace konative::jni {

// RAII wrapper around a single JNI LOCAL reference (jobject/jclass/jstring/...). Move-only - a
// local ref is a single logical resource; copying would let two wrappers both believe they own
// the same JNI local-ref-table slot.
//
// T must be a JNI reference type - enforced so this can never accidentally wrap a non-reference
// JNI type like jint or jmethodID, which DeleteLocalRef does not apply to.
template <typename T>
class LocalRef {
    static_assert(std::is_convertible_v<T, jobject>,
                  "LocalRef<T>: T must be a JNI reference type (jobject/jclass/jstring/...)");

public:
    LocalRef() noexcept = default;

    // Takes ownership of an ALREADY-ACQUIRED local ref (the result of FindClass, NewObject, a
    // Call*Method returning an object, NewStringUTF, ...) - never acquires one itself.
    LocalRef(JNIEnv* env, T obj) noexcept : env_(obj != nullptr ? env : nullptr), obj_(obj) {}

    ~LocalRef() { reset(); }

    LocalRef(const LocalRef&) = delete;
    LocalRef& operator=(const LocalRef&) = delete;

    LocalRef(LocalRef&& other) noexcept : env_(other.env_), obj_(other.obj_) { other.release_ownership(); }

    LocalRef& operator=(LocalRef&& other) noexcept {
        if (this != &other) {
            reset();
            env_ = other.env_;
            obj_ = other.obj_;
            other.release_ownership();
        }
        return *this;
    }

    explicit operator bool() const noexcept { return obj_ != nullptr; }
    T get() const noexcept { return obj_; }

    // Idempotent - safe to call multiple times or not at all.
    void reset() noexcept {
        if (obj_ != nullptr && env_ != nullptr) {
            env_->DeleteLocalRef(obj_);
        }
        release_ownership();
    }

private:
    void release_ownership() noexcept {
        env_ = nullptr;
        obj_ = nullptr;
    }

    JNIEnv* env_ = nullptr;
    T obj_ = nullptr;
};

// RAII wrapper around a single JNI GLOBAL reference. A distinct type from LocalRef<T> (not a
// template parameter/policy flag on one shared class) because the two have different threading
// contracts: a local ref is inherently single-JNIEnv*-instance scoped, while a global ref is valid
// from ANY attached thread and must be released via SOME valid JNIEnv* on ANY attached thread, not
// necessarily the one that created it.
template <typename T>
class GlobalRef {
    static_assert(std::is_convertible_v<T, jobject>,
                  "GlobalRef<T>: T must be a JNI reference type (jobject/jclass/jstring/...)");

public:
    GlobalRef() noexcept = default;

    // Promotes local_obj (an already-acquired, still-owned-by-the-caller local ref, or any other
    // raw T) to a NEW global ref via NewGlobalRef - does NOT take ownership of local_obj itself.
    GlobalRef(JNIEnv* env, T local_obj) {
        if (local_obj != nullptr) {
            obj_ = static_cast<T>(env->NewGlobalRef(local_obj));
        }
    }

    // Deliberately a no-op, NOT env-implicit - DeleteGlobalRef needs a JNIEnv*, and a JNIEnv* is
    // only ever valid for the currently-attached thread; there is no "ambient" JNIEnv* a
    // destructor could safely reach for. Callers must call reset(env) explicitly.
    ~GlobalRef() = default;

    GlobalRef(const GlobalRef&) = delete;
    GlobalRef& operator=(const GlobalRef&) = delete;

    GlobalRef(GlobalRef&& other) noexcept : obj_(other.obj_) { other.obj_ = nullptr; }

    GlobalRef& operator=(GlobalRef&& other) noexcept {
        if (this != &other) {
            assert(obj_ == nullptr &&
                   "GlobalRef<T> move-assigned over a still-live global ref - call reset(env) first");
            obj_ = other.obj_;
            other.obj_ = nullptr;
        }
        return *this;
    }

    explicit operator bool() const noexcept { return obj_ != nullptr; }
    T get() const noexcept { return obj_; }

    // MUST be called explicitly, with a valid env, before this wrapper is destroyed or replaced.
    // Idempotent, same contract as LocalRef<T>::reset().
    void reset(JNIEnv* env) noexcept {
        if (obj_ != nullptr && env != nullptr) {
            env->DeleteGlobalRef(obj_);
        }
        obj_ = nullptr;
    }

private:
    T obj_ = nullptr;
};

} // namespace konative::jni
