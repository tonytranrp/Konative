# The Kotlin/Native side (ARCHITECTURE.md \xc2\xa76/\xc2\xa77) - compiled by kotlinc-native via
# KonativeKotlinNative.cmake, never javac/kotlinc/d8 (that was the dex-embedding path this
# project explicitly moved away from - see ARCHITECTURE.md \xc2\xa71 and research/research.md).
#
# No CINTEROP_DEF needed here: Kotlin/Native ships pre-built EGL/GLES2/GLES3/Android cinterop
# bindings for every androidNative* target out of the box (platform.egl/platform.gles3/
# platform.android - see native/README.md and native/src/Renderer.kt's own top comment). A
# CINTEROP_DEF only becomes necessary if/when a genuinely un-bundled C API is needed (Vulkan is
# the known example - see native/cinterop/README.md).
konative_add_kotlin_native_module(app
  KOTLIN_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src
)
