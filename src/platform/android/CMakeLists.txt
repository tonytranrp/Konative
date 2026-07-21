# Produces the single fused .so: the JNI_OnLoad entry point (ARCHITECTURE.md section 6.4,
# replacing the deleted android_main.cpp/android_native_app_glue design, section 6.1), Konative's
# own C++ core, and the embedded, self-checking Kotlin+Compose dex blob (section 6.5/6.6) that owns
# rendering. "One .so, rendering + app logic together" (ARCHITECTURE.md section 1).
#
# Two ways to supply the embedded dex, in priority order:
#   1. -DKONATIVE_EMBEDDED_DEX_PATH=<path to a real classes.dex> - manual override, e.g. a dex
#      built by hand, or a machine without kotlinc/r8/aapt2 installed at all (a from-source CI
#      checkout). Optionally pair it with -DKONATIVE_EMBEDDED_RESOURCES_ARSC_PATH=<path to a real
#      resources.arsc> - see the branch below for why, and what happens if you don't.
#   2. Automated (default): konative_embed_kotlin_dex() compiles embedded_kotlin/src at BUILD time
#      via the real, hand-validated kotlinc+aapt2+r8 recipe (embedded_kotlin/README.md's Status
#      section) - requires KONATIVE_KOTLINC/KONATIVE_R8/KONATIVE_ANDROID_JAR/KONATIVE_AAPT2/
#      KONATIVE_JAVAC/KONATIVE_KOTLIN_CLASSPATH_DIR/KONATIVE_AAPT2_AAR_DIR to be set
#      (CMakeUserPresets.json, same machine-local-override pattern as ANDROID_NDK_HOME). Real
#      AAPT2-linked resource classes replace embedded_kotlin/r_shim/'s old hand-shimmed
#      placeholders - see embedded_kotlin/README.md for what this does and does not fix.
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
  # jni_onload.cpp unconditionally `extern`s a sibling konative_app_dex_resources_* blob (the
  # automated branch below always embeds one via KonativeEmbedKotlinDex.cmake) - if this branch
  # doesn't define those same 4 symbols too, the link fails outright with undefined-symbol errors
  # (this project's NDK toolchain passes -Wl,--no-undefined by default). A real bug of exactly this
  # shape was caught by a verify-subagent reviewing commit f231098, which added the resources blob
  # without updating this branch to match.
  #
  # KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH mirrors KONATIVE_EMBEDDED_DEX_PATH for a caller who has a
  # real resources.arsc to pair with their hand-built dex. If they don't, embed a real, empty
  # (0-byte) placeholder instead: jni_onload.cpp's own SHA-256 self-check still passes (the hash
  # honestly matches, since the blob really is empty), and Kotlin's
  # tryInstallGeneralResourcesLoader() safely falls back to the scoped patch when
  # ResourcesProvider.loadFromTable() rejects the empty table - the exact same non-fatal
  # exception-handling path already covering a corrupted real resources.arsc, not new behavior.
  # This matches testapp/README.md's already-documented contract that a null/absent resources
  # buffer is a valid, supported configuration for this manual-override path.
  if(KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH)
    set(_konative_app_resources_arsc_blob "${KONATIVE_EMBEDDED_RESOURCES_ARSC_PATH}")
  else()
    set(_konative_app_resources_arsc_blob
      "${CMAKE_CURRENT_BINARY_DIR}/konative_app_native_empty_resources.arsc")
    if(NOT EXISTS "${_konative_app_resources_arsc_blob}")
      file(WRITE "${_konative_app_resources_arsc_blob}" "")
    endif()
  endif()
  konative_embed_binary_blob(konative_app_native
    BLOB "${_konative_app_resources_arsc_blob}"
    SYMBOL konative_app_dex_resources
    VERIFY_SHA256
  )
else()
  file(GLOB_RECURSE _konative_app_kt_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../embedded_kotlin/src/*.kt"
  )
  konative_embed_kotlin_dex(konative_app_native
    SOURCES ${_konative_app_kt_sources}
    PG_CONF "${CMAKE_CURRENT_SOURCE_DIR}/../../../embedded_kotlin/r8-rules.pro"
    SYMBOL konative_app_dex
    VERIFY_SHA256
  )
endif()

konative_apply_warnings(konative_app_native)
