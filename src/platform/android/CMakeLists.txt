# Produces the single fused .so: android_native_app_glue's expected `android_main` entry point,
# Konative's own C++ core, and the linked Kotlin/Native static lib that owns ALL rendering
# (ARCHITECTURE.md \xc2\xa76 - there is no C++ GLES/Vulkan backend, see render/README.md).
# "One .so, rendering + app logic together" (ARCHITECTURE.md \xc2\xa71).

# android_native_app_glue.c ships as NDK *source*, not a prebuilt lib - every real CMake+NDK
# project compiles it itself. Swap this for GameActivity's own replacement
# (game-activity::game-activity_static) if the ARCHITECTURE.md \xc2\xa76.1 NativeActivity/GameActivity
# choice is later flipped - do not compile both at once.
add_library(konative_native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(konative_native_app_glue PUBLIC
  ${ANDROID_NDK}/sources/android/native_app_glue
)

add_library(konative_app_native SHARED
  android_main.cpp
  activity_bridge.cpp
  looper_pump.cpp
)

target_link_libraries(konative_app_native PRIVATE
  konative::konative
  konative::platform::android
  konative_native_app_glue
  android
  log
)

if(TARGET konative::kotlin::app)
  target_link_libraries(konative_app_native PRIVATE konative::kotlin::app)
endif()

# Required so the framework's own onAppCmd/android_main survive linking against a shared lib
# built with -Wl,--gc-sections (the default for Release NDK builds) - matches the -u
# ANativeActivity_onCreate flag every real NDK CMake+native_app_glue project needs.
target_link_options(konative_app_native PRIVATE -u ANativeActivity_onCreate)

konative_apply_warnings(konative_app_native)
