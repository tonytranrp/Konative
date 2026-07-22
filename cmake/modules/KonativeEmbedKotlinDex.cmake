# konative_embed_kotlin_dex(<target>
#   SOURCES <kt-file>...
#   CLASSPATH_DIR <dir>
#   AAPT2_AAR_DIR <dir>
#   PG_CONF <path>
#   SYMBOL <prefix>
#   [MIN_API <n>]
# )
#
# Automates the full, hand-validated embedded_kotlin/ pipeline (embedded_kotlin/README.md's Status
# section - real Compose UI, screenshotted, rendering on real hardware via this exact recipe run by
# hand): kotlinc (+ its bundled Compose compiler plugin) compiles <kt-file>... against
# CLASSPATH_DIR's jars, aapt2 links AAPT2_AAR_DIR's real AARs into real R classes (replacing
# embedded_kotlin/r_shim/'s hand-shimmed placeholders), then r8 shrinks+dexes the combined result
# using PG_CONF, then the resulting classes.dex is embedded into <target> via the existing
# konative_embed_binary_blob() (.incbin, build-time regeneration, optional SHA-256 self-check - see
# KonativeEmbedBlob.cmake). The actual compile+link+dex work happens at BUILD time in
# KonativeCompileKotlinDex.cmake (a `cmake -P` driver, same shape as
# KonativeGenerateIncbinAsm.cmake), not here at configure time - this file only wires up the
# add_custom_command/add_custom_target graph.
#
# CLASSPATH_DIR is a plain directory of pre-resolved .jar dependencies (Compose runtime/ui/
# foundation, activity, lifecycle-runtime/viewmodel, savedstate, kotlinx-coroutines-android,
# kotlin-stdlib - NOT android.jar, that's separate). Real Maven-dependency-resolution-from-CMake is
# still an open problem (embedded_kotlin/README.md, ARCHITECTURE.md section 6.6/6.7) - until it
# lands, this directory must be assembled by hand once per machine (the Gradle-as-pure-resolver
# scratchpad pattern documented in embedded_kotlin/README.md is the known-working way to produce
# it) and pointed at via KONATIVE_KOTLIN_CLASSPATH_DIR. This function does not attempt to hide that
# gap behind a fake default - see the FATAL_ERROR below.
#
# AAPT2_AAR_DIR is a plain directory of the real, UNMODIFIED .aar files (not just their extracted
# classes.jar - CLASSPATH_DIR's jars carry no res/ content at all) for the same dependency set
# CLASSPATH_DIR resolves. Real AARs are already cached locally as a side effect of whatever produced
# CLASSPATH_DIR (Gradle's own artifact cache keeps the original AAR alongside the extracted jar it
# hands to a runtime classpath resolution) - see embedded_kotlin/README.md's Status section for how
# this directory is currently assembled and pointed at via KONATIVE_AAPT2_AAR_DIR.
#
# NOTE on what this fixes (see embedded_kotlin/README.md's Status section for the full writeup):
# real, AAPT2-assigned values for every field embedded_kotlin/r_shim/ used to hand-shim (build-time/
# classload-time correctness), AND - since aapt2 link's own -o output already contains a complete,
# real resources.arsc, now kept instead of discarded - a second embedded blob feeding the general
# Resources.getString()/ResourcesLoader runtime mechanism (API 30+,
# embedded_kotlin/KonativeResourcesLoader.kt), with embedded_kotlin/KonativeResourceStringOverride
# .kt's smaller, scoped patch as the fallback below that API floor.
#
# Requires five machine-local toolchain cache variables/environment variables, following the exact
# same pattern as ANDROID_NDK_HOME/CMakeUserPresets.json (see that file and
# KonativeAndroidToolchain.cmake):
#   KONATIVE_KOTLINC      - path to a real kotlinc(.bat) that bundles the Compose compiler plugin
#                            at <bin-dir>/../lib/compose-compiler-plugin.jar (verified present in
#                            the Kotlin 2.4.0 distribution - see ARCHITECTURE.md section 6.6)
#   KONATIVE_R8            - path to a real r8(.bat) (Android SDK cmdline-tools/*/bin/r8(.bat))
#   KONATIVE_ANDROID_JAR    - path to a real android.jar (Android SDK platforms/android-<N>/android.jar)
#   KONATIVE_AAPT2          - path to a real aapt2(.exe) (Android SDK build-tools/*/aapt2(.exe))
#   KONATIVE_JAVAC          - path to a real javac(.exe) (a JDK's bin/javac), to compile aapt2's
#                             generated R.java output
#   KONATIVE_KOTLIN_CLASSPATH_DIR - default for CLASSPATH_DIR if the caller doesn't pass one explicitly
#   KONATIVE_AAPT2_AAR_DIR  - default for AAPT2_AAR_DIR if the caller doesn't pass one explicitly
#
# Any of the five machine-local variables left unset falls back to real auto-discovery below (never
# overrides an explicit value - CMakeUserPresets.json stays authoritative wherever it already sets
# one): KONATIVE_ANDROID_JAR/KONATIVE_AAPT2/KONATIVE_R8 scan $ENV{ANDROID_HOME} (or
# $ENV{ANDROID_SDK_ROOT}) for the newest platforms/android-*/build-tools/* directory present;
# KONATIVE_JAVAC uses find_package(Java); KONATIVE_KOTLINC uses a bare find_program() (kotlinc has no
# ANDROID_HOME-style well-known env var, so this one's real-world hit rate is low - most machines
# will still need it set explicitly). Verified empirically (a standalone `cmake -P` script run
# against this project's own real SDK install) to reproduce the exact paths CMakeUserPresets.json
# already has hand-written for android.jar/aapt2/r8/javac before being wired in here.

include_guard(GLOBAL)

function(konative_embed_kotlin_dex TARGET_NAME)
  cmake_parse_arguments(ARG "VERIFY_SHA256" "CLASSPATH_DIR;AAPT2_AAR_DIR;PG_CONF;SYMBOL;MIN_API" "SOURCES" ${ARGN})

  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): SOURCES <kt-file>... is required")
  endif()
  if(NOT ARG_CLASSPATH_DIR)
    set(ARG_CLASSPATH_DIR "${KONATIVE_KOTLIN_CLASSPATH_DIR}")
  endif()
  if(NOT ARG_CLASSPATH_DIR)
    message(FATAL_ERROR
      "konative_embed_kotlin_dex(${TARGET_NAME}): CLASSPATH_DIR <dir> is required (or set "
      "KONATIVE_KOTLIN_CLASSPATH_DIR) - a directory of pre-resolved .jar dependencies (Compose "
      "runtime/ui/foundation, activity, lifecycle-runtime/viewmodel, savedstate, "
      "kotlinx-coroutines-android, kotlin-stdlib). See embedded_kotlin/README.md for how this "
      "directory is currently produced (Maven resolution from CMake is a real open problem, not "
      "solved by this function) and CMakeUserPresets.json for the machine-local-override pattern "
      "already used for ANDROID_NDK_HOME.")
  endif()
  if(NOT ARG_AAPT2_AAR_DIR)
    set(ARG_AAPT2_AAR_DIR "${KONATIVE_AAPT2_AAR_DIR}")
  endif()
  if(NOT ARG_AAPT2_AAR_DIR)
    message(FATAL_ERROR
      "konative_embed_kotlin_dex(${TARGET_NAME}): AAPT2_AAR_DIR <dir> is required (or set "
      "KONATIVE_AAPT2_AAR_DIR) - a directory of the real, unmodified .aar files (NOT just their "
      "extracted classes.jar - CLASSPATH_DIR's jars have no res/ content) for the same dependency "
      "set CLASSPATH_DIR resolves, used to link real AAPT2-assigned resource IDs replacing "
      "embedded_kotlin/r_shim/'s hand-shimmed placeholders. See embedded_kotlin/README.md's Status "
      "section for how this directory is currently produced and CMakeUserPresets.json for the "
      "machine-local-override pattern.")
  endif()
  if(NOT ARG_PG_CONF)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): PG_CONF <path> is required - an r8 proguard-rules file, e.g. embedded_kotlin/r8-rules.pro")
  endif()
  if(NOT ARG_SYMBOL)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): SYMBOL <prefix> is required")
  endif()
  if(NOT ARG_MIN_API)
    # dalvik.system.InMemoryDexClassLoader's own minimum API level (embedded_kotlin/README.md).
    set(ARG_MIN_API 26)
  endif()
  if(NOT TARGET ${TARGET_NAME})
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): no such target - call add_library()/add_executable() before this function")
  endif()

  # Toolchain auto-discovery - attempted only as a fallback when a manual override (the primary,
  # always-reliable mechanism: CMakeUserPresets.json, exactly as documented above) isn't already
  # set. Best-effort, not guaranteed: real-tested against this project's own dev machine before
  # writing this (a standalone `cmake -P` script run against the real, already-known-correct SDK
  # install), not assumed to work from CMake/Android conventions alone - confirmed
  # $ENV{ANDROID_HOME}-based discovery finds the exact real android.jar/aapt2/r8 paths this
  # project's own CMakeUserPresets.json already has hardcoded, picking the newest
  # platforms/android-*` and `build-tools/*` version present via CMake's natural-sort `list(SORT
  # ... COMPARE NATURAL)`. `find_package(Java)` correctly discovers `javac` too. kotlinc has NO
  # discovery here beyond bare `find_program()`-via-PATH: unlike Android/Java, Kotlin has no
  # standard "well-known install location" env var convention, and it was confirmed NOT on PATH on
  # this project's own dev machine even with a real, working kotlinc installed elsewhere - expect
  # this one specifically to still usually need a manual override.
  if(NOT KONATIVE_KOTLINC)
    find_program(KONATIVE_KOTLINC NAMES kotlinc kotlinc.bat)
  endif()
  if(NOT KONATIVE_JAVAC)
    find_package(Java QUIET COMPONENTS Development)
    if(Java_JAVAC_EXECUTABLE)
      set(KONATIVE_JAVAC "${Java_JAVAC_EXECUTABLE}")
    endif()
  endif()
  if((NOT KONATIVE_R8 OR NOT KONATIVE_ANDROID_JAR OR NOT KONATIVE_AAPT2) AND
     (DEFINED ENV{ANDROID_HOME} OR DEFINED ENV{ANDROID_SDK_ROOT}))
    if(DEFINED ENV{ANDROID_HOME})
      set(_konative_sdk_root "$ENV{ANDROID_HOME}")
    else()
      set(_konative_sdk_root "$ENV{ANDROID_SDK_ROOT}")
    endif()
    if(NOT KONATIVE_ANDROID_JAR)
      file(GLOB _konative_platform_dirs LIST_DIRECTORIES true "${_konative_sdk_root}/platforms/android-*")
      if(_konative_platform_dirs)
        list(SORT _konative_platform_dirs COMPARE NATURAL ORDER DESCENDING)
        list(GET _konative_platform_dirs 0 _konative_newest_platform)
        if(EXISTS "${_konative_newest_platform}/android.jar")
          set(KONATIVE_ANDROID_JAR "${_konative_newest_platform}/android.jar")
        endif()
      endif()
    endif()
    if(NOT KONATIVE_AAPT2)
      file(GLOB _konative_build_tools_dirs LIST_DIRECTORIES true "${_konative_sdk_root}/build-tools/*")
      if(_konative_build_tools_dirs)
        list(SORT _konative_build_tools_dirs COMPARE NATURAL ORDER DESCENDING)
        list(GET _konative_build_tools_dirs 0 _konative_newest_build_tools)
        if(EXISTS "${_konative_newest_build_tools}/aapt2.exe")
          set(KONATIVE_AAPT2 "${_konative_newest_build_tools}/aapt2.exe")
        elseif(EXISTS "${_konative_newest_build_tools}/aapt2")
          set(KONATIVE_AAPT2 "${_konative_newest_build_tools}/aapt2")
        endif()
      endif()
    endif()
    if(NOT KONATIVE_R8)
      file(GLOB _konative_r8_candidates
        "${_konative_sdk_root}/cmdline-tools/*/bin/r8.bat"
        "${_konative_sdk_root}/cmdline-tools/*/bin/r8")
      if(_konative_r8_candidates)
        list(GET _konative_r8_candidates 0 KONATIVE_R8)
      endif()
    endif()
  endif()

  if(NOT KONATIVE_KOTLINC)
    message(FATAL_ERROR
      "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_KOTLINC must be set to a real "
      "kotlinc(.bat) path - add it to CMakeUserPresets.json's environment block, the same way "
      "ANDROID_NDK_HOME is already set there.")
  endif()
  if(NOT EXISTS "${KONATIVE_KOTLINC}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_KOTLINC (${KONATIVE_KOTLINC}) does not exist")
  endif()
  if(NOT KONATIVE_R8)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_R8 must be set to a real r8(.bat) path (Android SDK cmdline-tools/*/bin/r8(.bat))")
  endif()
  if(NOT EXISTS "${KONATIVE_R8}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_R8 (${KONATIVE_R8}) does not exist")
  endif()
  if(NOT KONATIVE_ANDROID_JAR)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_ANDROID_JAR must be set to a real android.jar path (Android SDK platforms/android-<N>/android.jar)")
  endif()
  if(NOT EXISTS "${KONATIVE_ANDROID_JAR}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_ANDROID_JAR (${KONATIVE_ANDROID_JAR}) does not exist")
  endif()
  if(NOT EXISTS "${ARG_CLASSPATH_DIR}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): CLASSPATH_DIR (${ARG_CLASSPATH_DIR}) does not exist")
  endif()
  if(NOT EXISTS "${ARG_AAPT2_AAR_DIR}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): AAPT2_AAR_DIR (${ARG_AAPT2_AAR_DIR}) does not exist")
  endif()
  if(NOT EXISTS "${ARG_PG_CONF}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): PG_CONF (${ARG_PG_CONF}) does not exist")
  endif()
  if(NOT KONATIVE_AAPT2)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_AAPT2 must be set to a real aapt2(.exe) path (Android SDK build-tools/*/aapt2(.exe))")
  endif()
  if(NOT EXISTS "${KONATIVE_AAPT2}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_AAPT2 (${KONATIVE_AAPT2}) does not exist")
  endif()
  if(NOT KONATIVE_JAVAC)
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_JAVAC must be set to a real javac(.exe) path (a JDK's bin/javac)")
  endif()
  if(NOT EXISTS "${KONATIVE_JAVAC}")
    message(FATAL_ERROR "konative_embed_kotlin_dex(${TARGET_NAME}): KONATIVE_JAVAC (${KONATIVE_JAVAC}) does not exist")
  endif()

  # The Compose compiler plugin ships bundled inside the kotlinc distribution itself, at
  # <kotlinc-bin-dir>/../lib/compose-compiler-plugin.jar - not a separate Maven download (verified
  # present in the real Kotlin 2.4.0 distribution used to build the working milestone; see
  # ARCHITECTURE.md section 6.6).
  get_filename_component(_kotlinc_bin_dir "${KONATIVE_KOTLINC}" DIRECTORY)
  get_filename_component(_compose_plugin "${_kotlinc_bin_dir}/../lib/compose-compiler-plugin.jar" ABSOLUTE)
  if(NOT EXISTS "${_compose_plugin}")
    message(FATAL_ERROR
      "konative_embed_kotlin_dex(${TARGET_NAME}): expected the Compose compiler plugin bundled at "
      "${_compose_plugin} (relative to KONATIVE_KOTLINC) but it doesn't exist - is KONATIVE_KOTLINC "
      "pointing at a kotlinc distribution that actually bundles it?")
  endif()

  # kotlin-stdlib.jar ALSO ships inside the kotlinc distribution (<kotlinc-bin-dir>/../lib/), and
  # kotlinc auto-adds it to its OWN compilation classpath implicitly (no -classpath entry needed
  # for that step) - but r8, when shrinking+dexing the compiled output afterward, needs it as a
  # real, explicit dexing INPUT like any other dependency jar. A real, reproduced gap: this
  # module's own smoke test confirmed CLASSPATH_DIR (a Gradle debugRuntimeClasspath resolution,
  # embedded_kotlin/README.md) does NOT contain kotlin-stdlib.jar at all - Gradle/AGP's Kotlin
  # plugin supplies it through a different mechanism that a plain runtimeClasspath file
  # collection doesn't capture - so r8 reported dozens of "Missing class kotlin.**" errors when it
  # wasn't included explicitly. Sourcing it from the kotlinc distribution itself (rather than
  # requiring it in CLASSPATH_DIR) also guarantees it's the exact version matching KONATIVE_KOTLINC,
  # not whatever version Gradle happened to resolve.
  get_filename_component(_kotlin_stdlib_jar "${_kotlinc_bin_dir}/../lib/kotlin-stdlib.jar" ABSOLUTE)
  if(NOT EXISTS "${_kotlin_stdlib_jar}")
    message(FATAL_ERROR
      "konative_embed_kotlin_dex(${TARGET_NAME}): expected kotlin-stdlib.jar bundled at "
      "${_kotlin_stdlib_jar} (relative to KONATIVE_KOTLINC) but it doesn't exist - is "
      "KONATIVE_KOTLINC pointing at a real kotlinc distribution directory structure?")
  endif()

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/konative_embed_kotlin_dex/${TARGET_NAME}")
  set(_dex_file "${_gen_dir}/dex/classes.dex")
  set(_resources_arsc_file "${_gen_dir}/resources.arsc")
  file(MAKE_DIRECTORY "${_gen_dir}")

  # Tracked as an explicit DEPENDS input (not just read at build time) so adding/removing/updating
  # an .aar in AAPT2_AAR_DIR correctly triggers a rebuild - same reasoning as ARG_SOURCES/PG_CONF
  # below, mirrored from CLASSPATH_DIR's own (pre-existing) lack of such tracking, which is fine
  # there only because that directory's contents already sat still for the whole life of this
  # function so far; do the more-correct thing here from the start.
  file(GLOB _aapt2_aar_dir_contents "${ARG_AAPT2_AAR_DIR}/*.aar")

  add_custom_command(
    OUTPUT "${_dex_file}" "${_resources_arsc_file}"
    COMMAND "${CMAKE_COMMAND}"
            "-DKONATIVE_KOTLINC=${KONATIVE_KOTLINC}"
            "-DKONATIVE_COMPOSE_PLUGIN=${_compose_plugin}"
            "-DKONATIVE_KOTLIN_STDLIB_JAR=${_kotlin_stdlib_jar}"
            "-DKONATIVE_R8=${KONATIVE_R8}"
            "-DKONATIVE_ANDROID_JAR=${KONATIVE_ANDROID_JAR}"
            "-DKONATIVE_CLASSPATH_DIR=${ARG_CLASSPATH_DIR}"
            "-DKONATIVE_AAPT2=${KONATIVE_AAPT2}"
            "-DKONATIVE_JAVAC=${KONATIVE_JAVAC}"
            "-DKONATIVE_AAPT2_AAR_DIR=${ARG_AAPT2_AAR_DIR}"
            "-DKONATIVE_PG_CONF=${ARG_PG_CONF}"
            "-DKONATIVE_MIN_API=${ARG_MIN_API}"
            "-DKONATIVE_SOURCES=${ARG_SOURCES}"
            "-DKONATIVE_GEN_DIR=${_gen_dir}"
            "-DKONATIVE_DEX_FILE=${_dex_file}"
            "-DKONATIVE_RESOURCES_ARSC_FILE=${_resources_arsc_file}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeCompileKotlinDex.cmake"
    DEPENDS ${ARG_SOURCES} "${ARG_PG_CONF}" ${_aapt2_aar_dir_contents} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeCompileKotlinDex.cmake"
    COMMENT "konative: compiling+dexing ${ARG_SYMBOL} (kotlinc + aapt2 -> r8) from ${CMAKE_CURRENT_FUNCTION_LIST_DIR}"
    VERBATIM)

  # A real target (not just the OUTPUT files) so `cmake --build --target <name>_kotlin_dex` can
  # rebuild just the Kotlin half for fast iteration, and so <target>'s own dependency edge is
  # explicit rather than implied only through target_sources() picking up the generated .S files.
  add_custom_target(${TARGET_NAME}_kotlin_dex DEPENDS "${_dex_file}" "${_resources_arsc_file}")
  add_dependencies(${TARGET_NAME} ${TARGET_NAME}_kotlin_dex)

  set(_verify_sha256_arg "")
  if(ARG_VERIFY_SHA256)
    set(_verify_sha256_arg VERIFY_SHA256)
  endif()
  konative_embed_binary_blob(${TARGET_NAME}
    BLOB "${_dex_file}"
    SYMBOL "${ARG_SYMBOL}"
    ${_verify_sha256_arg}
  )
  # Sibling blob, same mechanism, real values confirmed via a real `aapt2 dump resources` on this
  # exact output (KonativeCompileKotlinDex.cmake's own comment) - feeds the general
  # ResourcesLoader-based runtime mechanism (embedded_kotlin/KonativeResourcesLoader.kt, API 30+),
  # never required to exist by anything below that API floor (KonativeResourceStringOverride.kt's
  # scoped patch remains the fallback there) - always embedded regardless, since it costs a fixed,
  # small size (a single resources.arsc for this dependency closure, not per-consumer) and one
  # binary artifact is simpler to reason about than a config that sometimes has it and sometimes
  # doesn't.
  konative_embed_binary_blob(${TARGET_NAME}
    BLOB "${_resources_arsc_file}"
    SYMBOL "${ARG_SYMBOL}_resources"
    VERIFY_SHA256
  )
endfunction()
