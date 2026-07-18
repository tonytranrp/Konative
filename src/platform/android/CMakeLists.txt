# Produces the single fused .so: the JNI_OnLoad entry point (ARCHITECTURE.md section 6.4,
# replacing the deleted android_main.cpp/android_native_app_glue design, section 6.1), Konative's
# own C++ core, and the embedded, self-checking Kotlin+Compose dex blob (section 6.5/6.6) that owns
# rendering. "One .so, rendering + app logic together" (ARCHITECTURE.md section 1).
#
# Two ways to supply the embedded dex, in priority order:
#   1. -DKONATIVE_EMBEDDED_DEX_PATH=<path to a real classes.dex> - manual override, e.g. a dex
#      built by hand, or a machine without kotlinc/r8 installed at all (a from-source CI checkout).
#   2. Automated (default): konative_embed_kotlin_dex() compiles embedded_kotlin/src + r_shim at
#      BUILD time via the real, hand-validated kotlinc+r8 recipe (embedded_kotlin/README.md's
#      Status section) - requires KONATIVE_KOTLINC/KONATIVE_R8/KONATIVE_ANDROID_JAR/
#      KONATIVE_KOTLIN_CLASSPATH_DIR to be set (CMakeUserPresets.json, same machine-local-override
#      pattern as ANDROID_NDK_HOME).
add_library(konative_app_native SHARED
  jni_onload.cpp
)

target_link_libraries(konative_app_native PRIVATE
  konative::konative
  konative::jni
  konative::embed
  android
  log
)

if(KONATIVE_EMBEDDED_DEX_PATH)
  konative_embed_binary_blob(konative_app_native
    BLOB "${KONATIVE_EMBEDDED_DEX_PATH}"
    SYMBOL konative_app_dex
    VERIFY_SHA256
  )
else()
  file(GLOB_RECURSE _konative_app_kt_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../embedded_kotlin/src/*.kt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../embedded_kotlin/r_shim/*.kt"
  )
  konative_embed_kotlin_dex(konative_app_native
    SOURCES ${_konative_app_kt_sources}
    PG_CONF "${CMAKE_CURRENT_SOURCE_DIR}/../../../embedded_kotlin/r8-rules.pro"
    SYMBOL konative_app_dex
    VERIFY_SHA256
  )
endif()

konative_apply_warnings(konative_app_native)
