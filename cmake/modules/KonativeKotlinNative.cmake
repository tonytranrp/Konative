# Wraps `kotlinc-native` as an ordinary CMake custom-command build step, producing a static
# library + generated C-ABI header that a C++ target can link directly - no JNI, no dex.
# Mirrors GameHub's cmake/modules/KotlinNative.cmake (same underlying compiler, same
# "wrap a toolchain step as custom commands producing a linkable IMPORTED target" shape),
# generalized here into a reusable Konative-wide module rather than one project's narrow tool.
#
# See ARCHITECTURE.md section 6.3: linking a Kotlin/Native static lib into an NDK CMake C++ target has
# real, documented, UNRESOLVED community friction (libc++/libm symbol conflicts). This module
# does not paper over that - it is the mechanism, not a guarantee it links cleanly. Verify with a
# real spike on your pinned NDK + Kotlin/Native versions before relying on it.
#
# konative_add_kotlin_native_module(<name> KOTLIN_SRC_DIR <dir> [CINTEROP_DEF <file>])
# Produces:
#   - <name>_kotlin_native   (actual IMPORTED STATIC target)
#   - konative::kotlin::<name>  (public ALIAS - link against this, not the internal target,
#     so the embedding/invocation mechanics can change later without breaking callers)
function(konative_add_kotlin_native_module name)
  if(NOT ANDROID)
    return()
  endif()

  cmake_parse_arguments(ARG "" "KOTLIN_SRC_DIR;CINTEROP_DEF" "" ${ARGN})
  if(NOT ARG_KOTLIN_SRC_DIR)
    message(FATAL_ERROR "konative_add_kotlin_native_module(${name}): KOTLIN_SRC_DIR is required")
  endif()
  if(NOT KONATIVE_KOTLIN_NATIVE_TARGET)
    message(FATAL_ERROR "konative_add_kotlin_native_module(${name}): KONATIVE_KOTLIN_NATIVE_TARGET is unset - include KonativeAndroidToolchain.cmake first")
  endif()

  find_program(KOTLINC_NATIVE_EXECUTABLE NAMES kotlinc-native kotlinc-native.bat
    DOC "Path to the Kotlin/Native compiler (kotlinc-native-prebuilt-*.zip - NOT the JVM-targeting kotlinc)")
  if(NOT KOTLINC_NATIVE_EXECUTABLE)
    message(FATAL_ERROR
      "Konative: kotlinc-native not found on PATH for module '${name}' - install a Kotlin/Native "
      "prebuilt distribution and add its bin/ directory to PATH. Do not confuse this with the "
      "JVM-targeting kotlinc distribution - they are separate compiler toolchains.")
  endif()

  file(GLOB_RECURSE KONATIVE_KN_SOURCES CONFIGURE_DEPENDS "${ARG_KOTLIN_SRC_DIR}/*.kt")
  if(NOT KONATIVE_KN_SOURCES)
    message(FATAL_ERROR "konative_add_kotlin_native_module(${name}): no .kt sources found under ${ARG_KOTLIN_SRC_DIR}")
  endif()

  set(KN_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/kotlin-native-${name}")
  set(KN_LIB "${KN_OUT_DIR}/lib${name}.a")
  set(KN_HEADER "${KN_OUT_DIR}/${name}_api.h")
  file(MAKE_DIRECTORY "${KN_OUT_DIR}")

  set(KN_BUILD_TYPE_FLAG "-g")
  if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(KN_BUILD_TYPE_FLAG "-opt")
  endif()

  set(KN_CINTEROP_ARGS "")
  if(ARG_CINTEROP_DEF)
    set(KN_KLIB "${KN_OUT_DIR}/${name}_cinterop.klib")
    add_custom_command(
      OUTPUT "${KN_KLIB}"
      COMMAND "${CMAKE_COMMAND}" -E echo "Konative: cinterop for ${name} not yet wired - see KONATIVE_KOTLIN_NATIVE_TARGET / ARCHITECTURE.md section 6.3"
      COMMENT "Konative: cinterop stub for ${name} (${ARG_CINTEROP_DEF})"
      VERBATIM)
    list(APPEND KN_CINTEROP_ARGS "-library" "${KN_KLIB}")
  endif()

  add_custom_command(
    OUTPUT "${KN_LIB}" "${KN_HEADER}"
    COMMAND "${KOTLINC_NATIVE_EXECUTABLE}" ${KONATIVE_KN_SOURCES}
            -produce static
            -target ${KONATIVE_KOTLIN_NATIVE_TARGET}
            ${KN_BUILD_TYPE_FLAG}
            ${KN_CINTEROP_ARGS}
            -o "${KN_OUT_DIR}/${name}"
    DEPENDS ${KONATIVE_KN_SOURCES}
    COMMENT "Konative: kotlinc-native compiling ${name} (${KONATIVE_KOTLIN_NATIVE_TARGET})"
    VERBATIM)

  add_custom_target(${name}_kotlin_native_build DEPENDS "${KN_LIB}" "${KN_HEADER}")

  add_library(${name}_kotlin_native STATIC IMPORTED GLOBAL)
  set_target_properties(${name}_kotlin_native PROPERTIES
    IMPORTED_LOCATION "${KN_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${KN_OUT_DIR}")
  add_dependencies(${name}_kotlin_native ${name}_kotlin_native_build)

  add_library(konative::kotlin::${name} ALIAS ${name}_kotlin_native)
endfunction()
