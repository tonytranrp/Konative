#pragma once

#include <string>

#include <jni.h>

#include "konative/core/result.hpp"
#include "konative/jni/call.hpp"
#include "konative/jni/ref.hpp"

namespace konative::jni {

// Which step of get_files_dir_path() below failed - same "name the specific step" convention as
// dex_loader.hpp's DexLoadError, per this module's own README Hard Rule (Result<T, E> with a real
// error enum, never a falsy-value-on-failure return, for anything fallible here).
enum class FilesDirError {
    kNullArguments,          // env or context was null - a caller bug, not a JNI failure
    kGetFilesDirFailed,      // Context.getFilesDir() threw or returned null
    kGetAbsolutePathFailed,  // File.getAbsolutePath() threw or returned null
    kStringConversionFailed, // GetStringUTFChars() threw or returned null
};

// Resolves context.getFilesDir().getAbsolutePath() - the app's private, always-writable internal
// storage directory (no permission needed, exists for every installed app) - into a real
// std::string the C++ side can hand to plain std::filesystem/fstream code. Both JNI signatures
// verified against the real vendored android.jar via javap before writing this (the standing
// discipline for any Android API used from native code): android.content.Context.getFilesDir() is
// ()Ljava/io/File; and java.io.File.getAbsolutePath() is ()Ljava/lang/String;. getFilesDir() is
// documented to never return null on a fully-attached Context (it also creates the directory if
// missing), and an android.app.Application obtained via ActivityThread.currentApplication() inside
// JNI_OnLoad is exactly that - the same object dex_loader.hpp already relies on for the
// classloader. call.hpp's helpers have already logged the JNI specifics by the time an error
// Result is returned; the enum tells the caller which step to blame.
inline core::Result<std::string, FilesDirError> get_files_dir_path(JNIEnv* env, jobject context) {
    using R = core::Result<std::string, FilesDirError>;
    if (env == nullptr || context == nullptr) {
        return R::err(FilesDirError::kNullArguments);
    }

    LocalRef<jobject> files_dir(
        env, call_instance_method<jobject>(env, context, "getFilesDir", "()Ljava/io/File;"));
    if (!files_dir) {
        return R::err(FilesDirError::kGetFilesDirFailed);
    }

    LocalRef<jstring> absolute_path(
        env, static_cast<jstring>(call_instance_method<jobject>(
                 env, files_dir.get(), "getAbsolutePath", "()Ljava/lang/String;")));
    if (!absolute_path) {
        return R::err(FilesDirError::kGetAbsolutePathFailed);
    }

    const char* chars = env->GetStringUTFChars(absolute_path.get(), nullptr);
    if (check_and_clear_exception(env, "GetStringUTFChars(getAbsolutePath)") || chars == nullptr) {
        return R::err(FilesDirError::kStringConversionFailed);
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(absolute_path.get(), chars);
    return R::ok(std::move(result));
}

} // namespace konative::jni
