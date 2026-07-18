# Build-time driver invoked via `cmake -P` from konative_embed_kotlin_dex()'s add_custom_command -
# same "regenerate at build time, not configure time" shape as KonativeGenerateIncbinAsm.cmake.
# Compiles Kotlin sources with the bundled Compose compiler plugin, then shrinks+dexes the result
# with r8 - the exact recipe validated by hand across many iterations (see embedded_kotlin/README.md's
# Status section and embedded_kotlin/r8-rules.pro's own comments for the real bugs this recipe's
# exact shape works around; don't simplify any step here without re-reading those first).

foreach(_required
    KONATIVE_KOTLINC KONATIVE_COMPOSE_PLUGIN KONATIVE_KOTLIN_STDLIB_JAR KONATIVE_R8 KONATIVE_ANDROID_JAR
    KONATIVE_CLASSPATH_DIR KONATIVE_PG_CONF KONATIVE_MIN_API KONATIVE_SOURCES
    KONATIVE_GEN_DIR KONATIVE_DEX_FILE)
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
