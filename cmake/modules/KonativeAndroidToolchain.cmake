# Derives the Kotlin/Native `-target` value from the NDK toolchain file's OWN already-established
# cache variables (ANDROID_ABI) instead of re-deriving ABI/API-level mapping independently - the
# same pattern corrosion's FindRust.cmake uses for Rust target triples and GameHub's own
# KotlinNative.cmake uses for its `gamehub_kotlin_native_target_for_abi()`. See ARCHITECTURE.md \xc2\xa77.
#
# kotlinc-native's target names for Android are lower-level than the Gradle/KMP-facing ones
# (android_arm64, not androidNativeArm64) - using the Gradle name fails with "Unknown target".
if(NOT ANDROID)
  message(FATAL_ERROR "KonativeAndroidToolchain.cmake included without ANDROID set - include only under an Android NDK toolchain configure")
endif()

if(ANDROID_ABI STREQUAL "arm64-v8a")
  set(KONATIVE_KOTLIN_NATIVE_TARGET "android_arm64")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
  set(KONATIVE_KOTLIN_NATIVE_TARGET "android_arm32")
elseif(ANDROID_ABI STREQUAL "x86_64")
  set(KONATIVE_KOTLIN_NATIVE_TARGET "android_x64")
elseif(ANDROID_ABI STREQUAL "x86")
  set(KONATIVE_KOTLIN_NATIVE_TARGET "android_x86")
else()
  message(FATAL_ERROR "Konative: unrecognized ANDROID_ABI '${ANDROID_ABI}' - no known Kotlin/Native target mapping")
endif()

message(STATUS "Konative: ANDROID_ABI=${ANDROID_ABI} -> Kotlin/Native target ${KONATIVE_KOTLIN_NATIVE_TARGET}")
