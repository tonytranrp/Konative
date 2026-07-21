# SUPERSEDED FOR RENDERING, confirmed landed (ARCHITECTURE.md section 6.7/9, native/README.md) -
# frozen, historical, do not extend for rendering. The Kotlin/Native side - compiled ahead-of-time
# by kotlinc-native via KonativeKotlinNative.cmake, never javac/kotlinc/d8. Confusingly, THIS
# project's own rendering history went dex-embedding (very first research pass) -> Kotlin/Native
# AOT+EGL (this module, moved away from dex-embedding) -> JVM-hosted Compose, dex-embedded again
# (current, final - ARCHITECTURE.md section 6.6/6.7, embedded_kotlin/) - so "dex-embedding" is
# simultaneously this module's abandoned past AND the live rendering path today, just via a totally
# different, unrelated kotlinc+aapt2+r8 pipeline than whatever the original research pass explored.
#
# No CINTEROP_DEF needed here: Kotlin/Native ships pre-built EGL/GLES2/GLES3/Android cinterop
# bindings for every androidNative* target out of the box (platform.egl/platform.gles3/
# platform.android - see native/README.md and native/src/Renderer.kt's own top comment). A
# CINTEROP_DEF only becomes necessary if/when a genuinely un-bundled C API is needed (Vulkan is
# the known example - see native/cinterop/README.md).
konative_add_kotlin_native_module(app
  KOTLIN_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src
)
