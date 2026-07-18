# Produces the single fused .so: the JNI_OnLoad entry point (ARCHITECTURE.md section 6.4,
# replacing the deleted android_main.cpp/android_native_app_glue design, section 6.1), Konative's
# own C++ core, and the embedded, self-checking Kotlin+Compose dex blob (section 6.5/6.6) that owns
# rendering. "One .so, rendering + app logic together" (ARCHITECTURE.md section 1).
#
# KONATIVE_EMBEDDED_DEX_PATH must point at a real classes.dex built by kotlinc+d8 (the automated
# konative_embed_kotlin_dex()-style CMake pipeline described in section 6.6 doesn't exist yet - set
# this manually to a dex built by hand in the meantime; see ARCHITECTURE.md section 6.7's status
# table for exactly what's landed vs. still open).
if(NOT KONATIVE_EMBEDDED_DEX_PATH)
  message(FATAL_ERROR
    "konative_app_native requires -DKONATIVE_EMBEDDED_DEX_PATH=<path to a real classes.dex> - "
    "the automated kotlinc+d8 CMake pipeline (ARCHITECTURE.md section 6.6) doesn't exist yet, so "
    "this must be built by hand for now (kotlinc -include-runtime -jvm-target 1.8 ... | d8 "
    "--min-api 26 ...) and passed explicitly.")
endif()

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

konative_embed_binary_blob(konative_app_native
  BLOB "${KONATIVE_EMBEDDED_DEX_PATH}"
  SYMBOL konative_app_dex
  VERIFY_SHA256
)

konative_apply_warnings(konative_app_native)
