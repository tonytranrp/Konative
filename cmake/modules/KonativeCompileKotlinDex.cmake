# Build-time driver invoked via `cmake -P` from konative_embed_kotlin_dex()'s add_custom_command -
# same "regenerate at build time, not configure time" shape as KonativeGenerateIncbinAsm.cmake.
# Compiles Kotlin sources with the bundled Compose compiler plugin, then shrinks+dexes the result
# with r8 - the exact recipe validated by hand across many iterations (see embedded_kotlin/README.md's
# Status section and embedded_kotlin/r8-rules.pro's own comments for the real bugs this recipe's
# exact shape works around; don't simplify any step here without re-reading those first).

foreach(_required
    KONATIVE_KOTLINC KONATIVE_COMPOSE_PLUGIN KONATIVE_KOTLIN_STDLIB_JAR KONATIVE_R8 KONATIVE_ANDROID_JAR
    KONATIVE_CLASSPATH_DIR KONATIVE_PG_CONF KONATIVE_MIN_API KONATIVE_SOURCES
    KONATIVE_GEN_DIR KONATIVE_DEX_FILE
    KONATIVE_AAPT2 KONATIVE_JAVAC KONATIVE_AAPT2_AAR_DIR)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: ${_required} must be passed via -D")
  endif()
endforeach()

set(_classes_dir "${KONATIVE_GEN_DIR}/classes")
set(_dex_dir "${KONATIVE_GEN_DIR}/dex")
file(REMOVE_RECURSE "${_classes_dir}")
file(MAKE_DIRECTORY "${_classes_dir}")
file(REMOVE_RECURSE "${_dex_dir}")
file(MAKE_DIRECTORY "${_dex_dir}")

# Real dependency jars are gathered from KONATIVE_CLASSPATH_DIR at BUILD time (a plain directory
# of pre-resolved .jar files, including kotlin-stdlib.jar - see embedded_kotlin/README.md for how
# this directory is currently produced; a real Maven-resolution step is still open, see
# ARCHITECTURE.md section 6.6/6.7) - not a hardcoded list, so re-running this build after the
# directory's contents change picks up the new jars automatically.
file(GLOB _classpath_jars "${KONATIVE_CLASSPATH_DIR}/*.jar")
if(NOT _classpath_jars)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: no .jar files found in KONATIVE_CLASSPATH_DIR (${KONATIVE_CLASSPATH_DIR})")
endif()

# --- Step 1: kotlinc, with the Compose compiler plugin, real Kotlin -> real .class bytecode ---
# -Xcompiler-plugin (NOT the older -Xplugin) is the correct flag for this K2/FIR-based plugin
# (ARCHITECTURE.md section 6.6 / iteration 3's spike - -Xplugin threw NoClassDefFoundError).
#
# An @argfile, NOT a direct -classpath "<jar1>;<jar2>;..." argument, because passing a single
# semicolon-joined argument directly breaks silently on this toolchain: kotlinc.bat's own argument
# handling truncates at the first `;` and treats everything after it as additional SOURCE FILES
# ("error: source entry is not a Kotlin file: ...") rather than as part of the classpath value -
# a real, reproduced bug (not a Git-Bash/MSYS quoting issue - confirmed by testing
# MSYS_NO_PATHCONV=1, which did not fix it). One argument per line in the file sidesteps whatever
# is actually mis-parsing this, entirely.
#
# Plain ";" here, NOT the "\;" CMake list-separator escape - this string is going straight into a
# FILE's contents (not being re-parsed as a CMake argument/list anywhere downstream), so "\;" would
# be wrong: a real, reproduced bug caught by this module's own smoke test - "\;" left a literal
# backslash in the argfile ("jar1\;jar2\;...") which kotlinc's classpath parser doesn't recognize
# as a separator at all, silently resolving zero classpath entries (surfaced as "unresolved
# reference" on plain android.os.Bundle/Activity, not a classpath-specific error).
set(_kotlinc_classpath "")
foreach(_jar IN LISTS _classpath_jars)
  string(APPEND _kotlinc_classpath "${_jar};")
endforeach()
string(APPEND _kotlinc_classpath "${KONATIVE_ANDROID_JAR}")

# kotlinc's @argfile format follows the standard JDK response-file convention
# (https://docs.oracle.com/javase/9/tools/java.htm#JSWOR-GUID-4856361B-8BFD-4964-AE84-121F5F6CF111):
# each line is whitespace-split into further tokens UNLESS wrapped in double quotes - confirmed by
# this module's own smoke test, where an unquoted classpath value containing
# "C:/Program Files (x86)/.../android.jar" (Android SDK's own real, common install location under
# Program Files) silently split at the space, dropping android.jar off the classpath entirely and
# surfacing as "unresolved reference: Activity/Bundle" with no classpath-related error at all - not
# a hypothetical, every standalone-line value below is quoted because of this real, reproduced bug.
#
# -Xcompiler-plugin=<path> can't use the two-line "-classpath\n\"<value>\"" form above - it's an
# =-joined single token, not a space-separated flag/value pair, so the whole line can't just be
# wrapped in quotes without also quoting the "-Xcompiler-plugin=" prefix (which kotlinc would then
# fail to recognize as the flag at all). Confirmed empirically (repro: a dummy jar under a
# space-containing scratch path, compiled via kotlinc.bat @argfile) that quoting only the path
# portion within the token - "-Xcompiler-plugin=\"<path>\"" - tokenizes correctly: the unquoted form
# truncated at the space ("error: no plugins found in given classpath: .../space"), the
# partial-quoted form resolved the full path (proceeded to actually try opening the jar). This is
# the same real bug class as the classpath/android.jar one above, just for KONATIVE_COMPOSE_PLUGIN's
# own path (wherever the kotlinc distribution's plugin jar unpacks to - not guaranteed space-free
# just because this dev machine's kotlinc happens to be under a space-free path).
set(_kotlinc_argfile "${KONATIVE_GEN_DIR}/kotlinc_args.txt")
file(WRITE "${_kotlinc_argfile}" "-jvm-target\n1.8\n-Xcompiler-plugin=\"${KONATIVE_COMPOSE_PLUGIN}\"\n-classpath\n\"${_kotlinc_classpath}\"\n-d\n\"${_classes_dir}\"\n")
foreach(_src IN LISTS KONATIVE_SOURCES)
  file(APPEND "${_kotlinc_argfile}" "\"${_src}\"\n")
endforeach()

execute_process(
  COMMAND "${KONATIVE_KOTLINC}" "@${_kotlinc_argfile}"
  RESULT_VARIABLE _kotlinc_result
  OUTPUT_VARIABLE _kotlinc_output
  ERROR_VARIABLE _kotlinc_error
)
if(NOT _kotlinc_result EQUAL 0)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: kotlinc failed (exit ${_kotlinc_result}):\n${_kotlinc_output}\n${_kotlinc_error}")
endif()

file(GLOB_RECURSE _class_files "${_classes_dir}/*.class")
if(NOT _class_files)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: kotlinc produced zero .class files in ${_classes_dir}")
endif()

# --- Step 1.5: aapt2, real resource-ID linking - replaces embedded_kotlin/r_shim/'s hand-shimmed
# placeholder R$id/R$string/etc. classes with real, AAPT2-assigned values, compiled straight into
# ${_classes_dir} so Step 2's R8 invocation picks them up with no changes of its own.
#
# Why this exists: published AndroidX AARs ship R.txt with every value as a literal placeholder
# 0x0 (real IDs are assigned per-final-app by AAPT2's link step; there is no "real" value to copy),
# and no AAR's classes.jar contains a compiled R.class at all (real or placeholder) - confirmed by
# direct inspection. r_shim/ used to hand-guess self-consistent replacement values instead, one
# field at a time, only after an on-device crash - this step derives real values mechanically,
# up front, for the whole dependency closure at once (real, reproduced repro: 0.35s for all 12
# libraries below, see the research this is based on).
#
# What this DOES fix: build-time/classload-time correctness (R8 "missing class"/NoSuchFieldError) -
# the entire, actual problem r_shim/ was built to solve. What this does NOT fix (deliberately, not
# an oversight - see embedded_kotlin/README.md's Status section for the full writeup): fields
# ultimately backed by Resources.getString()/a real resources.arsc table (R$string/R$style/
# R$styleable/etc.) still won't resolve at true runtime, because neither this embedded module nor
# testapp/'s own APK packages any res/ content - that is a separate, deeper, deliberately-deferred
# problem needing a real decision about testapp/'s own resource-packaging scope, not something to
# silently paper over here.
file(GLOB _aapt2_aars "${KONATIVE_AAPT2_AAR_DIR}/*.aar")
if(NOT _aapt2_aars)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: no .aar files found in KONATIVE_AAPT2_AAR_DIR (${KONATIVE_AAPT2_AAR_DIR})")
endif()

set(_aapt2_extract_dir "${KONATIVE_GEN_DIR}/aapt2_extract")
set(_aapt2_compiled_dir "${KONATIVE_GEN_DIR}/aapt2_compiled")
set(_aapt2_link_dir "${KONATIVE_GEN_DIR}/aapt2_link")
file(REMOVE_RECURSE "${_aapt2_extract_dir}" "${_aapt2_compiled_dir}" "${_aapt2_link_dir}")
file(MAKE_DIRECTORY "${_aapt2_extract_dir}" "${_aapt2_compiled_dir}" "${_aapt2_link_dir}")

set(_aapt2_compiled_zips "")
set(_aapt2_packages "")
foreach(_aar IN LISTS _aapt2_aars)
  get_filename_component(_aar_name "${_aar}" NAME_WE)
  set(_aar_extract_dir "${_aapt2_extract_dir}/${_aar_name}")
  file(MAKE_DIRECTORY "${_aar_extract_dir}")
  file(ARCHIVE_EXTRACT INPUT "${_aar}" DESTINATION "${_aar_extract_dir}")

  # A real, published AAR's own top-level AndroidManifest.xml is plain text (unlike a built APK's
  # compiled binary XML) - confirmed by direct inspection - so package="..." is a real, mechanically
  # extractable string every time, not a value to hand-maintain in a lookup table.
  if(NOT EXISTS "${_aar_extract_dir}/AndroidManifest.xml")
    message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: ${_aar} has no AndroidManifest.xml after extraction")
  endif()
  file(STRINGS "${_aar_extract_dir}/AndroidManifest.xml" _aapt2_manifest_line REGEX "package=\"[^\"]*\"" LIMIT_COUNT 1)
  string(REGEX MATCH "package=\"([^\"]*)\"" _aapt2_pkg_match "${_aapt2_manifest_line}")
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: could not find package=\"...\" in ${_aar}'s AndroidManifest.xml")
  endif()
  list(APPEND _aapt2_packages "${CMAKE_MATCH_1}")

  if(NOT EXISTS "${_aar_extract_dir}/res")
    # Real and legitimate, not an error: some AARs carry no res/ folder (e.g. metadata-only or
    # pure-Kotlin artifacts) - nothing to compile for this one, its package name is still collected
    # above in case another AAR's own resources reference it.
    continue()
  endif()
  set(_aapt2_compiled_zip "${_aapt2_compiled_dir}/${_aar_name}.zip")
  execute_process(
    COMMAND "${KONATIVE_AAPT2}" compile --dir "${_aar_extract_dir}/res" -o "${_aapt2_compiled_zip}"
    RESULT_VARIABLE _aapt2_compile_result
    OUTPUT_VARIABLE _aapt2_compile_output
    ERROR_VARIABLE _aapt2_compile_error
  )
  if(NOT _aapt2_compile_result EQUAL 0)
    message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: aapt2 compile failed for ${_aar} (exit ${_aapt2_compile_result}):\n${_aapt2_compile_output}\n${_aapt2_compile_error}")
  endif()
  list(APPEND _aapt2_compiled_zips "${_aapt2_compiled_zip}")
endforeach()

if(NOT _aapt2_compiled_zips)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: none of the AARs in KONATIVE_AAPT2_AAR_DIR (${KONATIVE_AAPT2_AAR_DIR}) had a res/ folder - nothing for aapt2 to link")
endif()

# aapt2 link's --min-sdk-version/--target-sdk-version flags fully substitute for a real
# <uses-sdk> - empirically confirmed via a standalone repro (omitting <uses-sdk> entirely from a
# 3-line manifest still linked cleanly) - so no manifest templating/merging is needed here.
set(_aapt2_manifest "${KONATIVE_GEN_DIR}/aapt2_manifest/AndroidManifest.xml")
file(WRITE "${_aapt2_manifest}" "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\" package=\"com.konative.generated\"></manifest>\n")

string(REGEX MATCH "android-([0-9]+)" _aapt2_api_match "${KONATIVE_ANDROID_JAR}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: could not determine a target API level from KONATIVE_ANDROID_JAR (${KONATIVE_ANDROID_JAR}) - expected a .../platforms/android-<N>/android.jar path")
endif()
set(_aapt2_target_api "${CMAKE_MATCH_1}")

list(JOIN _aapt2_packages ":" _aapt2_extra_packages)
execute_process(
  COMMAND "${KONATIVE_AAPT2}" link
          -I "${KONATIVE_ANDROID_JAR}"
          --manifest "${_aapt2_manifest}"
          -o "${_aapt2_link_dir}/linked.apk"
          --java "${_aapt2_link_dir}/java"
          --output-text-symbols "${_aapt2_link_dir}/R.txt"
          --extra-packages "${_aapt2_extra_packages}"
          --auto-add-overlay
          --min-sdk-version "${KONATIVE_MIN_API}"
          --target-sdk-version "${_aapt2_target_api}"
          ${_aapt2_compiled_zips}
  RESULT_VARIABLE _aapt2_link_result
  OUTPUT_VARIABLE _aapt2_link_output
  ERROR_VARIABLE _aapt2_link_error
)
if(NOT _aapt2_link_result EQUAL 0)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: aapt2 link failed (exit ${_aapt2_link_result}):\n${_aapt2_link_output}\n${_aapt2_link_error}")
endif()

file(GLOB_RECURSE _aapt2_java_files "${_aapt2_link_dir}/java/*.java")
if(NOT _aapt2_java_files)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: aapt2 link reported success but produced zero R.java files under ${_aapt2_link_dir}/java")
endif()

# No -bootclasspath: confirmed by direct inspection that aapt2's generated R.java files have zero
# android.* imports (they're pure nested classes of int/String constants) - a real, reproduced
# javac error ("option --boot-class-path not allowed with target 17") when it was included, since
# -bootclasspath is only valid when cross-compiling to an OLDER -source/-target than the running
# JDK's own implicit default, which these files have no need to do.
execute_process(
  COMMAND "${KONATIVE_JAVAC}" -d "${_classes_dir}" ${_aapt2_java_files}
  RESULT_VARIABLE _aapt2_javac_result
  OUTPUT_VARIABLE _aapt2_javac_output
  ERROR_VARIABLE _aapt2_javac_error
)
if(NOT _aapt2_javac_result EQUAL 0)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: javac failed compiling aapt2-generated R classes (exit ${_aapt2_javac_result}):\n${_aapt2_javac_output}\n${_aapt2_javac_error}")
endif()

message(STATUS "konative: aapt2-linked real R classes for ${_aapt2_extra_packages}")

# Re-glob now that Step 1.5 has added the real R*.class files alongside kotlinc's own output -
# Step 2 below dexes everything in ${_classes_dir} in one pass, real R classes included.
file(GLOB_RECURSE _class_files "${_classes_dir}/*.class")
if(NOT _class_files)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: ${_classes_dir} is unexpectedly empty after kotlinc+aapt2/javac")
endif()

# --- Step 2: r8, shrink + dex in one pass (not d8 - a naive d8-dexed full Compose+lifecycle+
# savedstate+coroutines runtime is ~20MB and splits into 2 dex files, which
# konative::jni::load_class_from_dex() only accepts one buffer of; r8's shrinking is what gets
# this down to a single ~1-2MB dex - see embedded_kotlin/README.md's Status section for the
# measured numbers). KONATIVE_PG_CONF (embedded_kotlin/r8-rules.pro) carries every real,
# evidence-backed rule this exact recipe needs - see that file's own comments, don't touch this
# step's flags without reading it first. Real dependency jars' own bundled consumer R8 rules
# (META-INF/com.android.tools/r8/*.pro etc.) are auto-discovered by r8 from its own inputs, not
# duplicated here.
# UNLIKE kotlinc's @argfile (javac-style, whitespace-split, double-quote-aware - see above), r8's
# @argfile does a raw one-path-per-line read with NO quote-stripping at all - a real, reproduced
# difference caught by this module's own smoke test: wrapping these paths in double quotes (as the
# kotlinc fix above requires) made r8 crash with "java.nio.file.InvalidPathException: Illegal char
# <\"> at index 0", because it fed the literal quote character straight to java.nio.file.Paths.get().
# Left unquoted here - KONATIVE_GEN_DIR is a build directory, not guaranteed space-free on every
# machine, but r8's own argfile format gives no safe way to handle that if it ever happens; the
# --output/--lib/--pg-conf arguments below are passed as real argv entries (not through this
# argfile) so CMake's own execute_process() handles spaces in THOSE correctly regardless.
set(_class_files_argfile "${KONATIVE_GEN_DIR}/class_files.txt")
file(WRITE "${_class_files_argfile}" "")
foreach(_class_file IN LISTS _class_files)
  file(APPEND "${_class_files_argfile}" "${_class_file}\n")
endforeach()

# kotlin-stdlib.jar is a real dexing input (see KonativeEmbedKotlinDex.cmake's own comment on why
# it's sourced from the kotlinc distribution rather than expected inside KONATIVE_CLASSPATH_DIR) -
# added alongside the Gradle-resolved dependency jars, not in place of them.
execute_process(
  COMMAND "${KONATIVE_R8}"
          --min-api "${KONATIVE_MIN_API}"
          --release
          --lib "${KONATIVE_ANDROID_JAR}"
          --output "${_dex_dir}"
          --pg-conf "${KONATIVE_PG_CONF}"
          "@${_class_files_argfile}"
          "${KONATIVE_KOTLIN_STDLIB_JAR}"
          ${_classpath_jars}
  RESULT_VARIABLE _r8_result
  OUTPUT_VARIABLE _r8_output
  ERROR_VARIABLE _r8_error
)
if(NOT _r8_result EQUAL 0)
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: r8 failed (exit ${_r8_result}):\n${_r8_output}\n${_r8_error}")
endif()

set(_produced_dex "${_dex_dir}/classes.dex")
if(NOT EXISTS "${_produced_dex}")
  message(FATAL_ERROR "KonativeCompileKotlinDex.cmake: r8 reported success but ${_produced_dex} does not exist")
endif()
file(RENAME "${_produced_dex}" "${KONATIVE_DEX_FILE}")

message(STATUS "konative: compiled+dexed ${KONATIVE_DEX_FILE}")
