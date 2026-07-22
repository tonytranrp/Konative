# konative_embed_binary_blob(<target> BLOB <path> SYMBOL <prefix> [VERIFY_SHA256])
#
# Embeds an arbitrary binary file as linked read-only data inside <target> via
# a GAS `.incbin` directive, assembled by the same NDK Clang/LLD invocation as
# <target>'s other sources - see research/incbin_embedding_research.md for the
# full rationale/citations behind every choice below (C-array text generation
# chokes lexing a multi-MB generated .cpp - research/research.md section 3.6;
# objcopy -I binary's symbol names are path-derived and unstable - section 5
# of the above).
#
# Exposes, for any TU to `extern "C"` declare:
#   extern const unsigned char <prefix>_start[];
#   extern const unsigned char <prefix>_end[];
#   extern const uint64_t      <prefix>_size;   // both target ABIs are LP64
#
# PRECONDITION: the including project must already have ASM enabled
# (`enable_language(ASM)` or `project(... LANGUAGES ... ASM)`) before calling
# this function - Konative's own top-level CMakeLists.txt does this inside its
# `if(ANDROID)` block. This function deliberately does NOT call
# enable_language(ASM) itself: a real verification pass proved that calling it
# for the FIRST time from inside a CMake function (as opposed to a top-level
# CMakeLists.txt) fails outright - "Missing variable is:
# CMAKE_ASM_COMPILE_OBJECT" - on this project's actual toolchain (NDK r28 +
# Ninja + CMake 4.3.2). An earlier version of this module called it here
# anyway on the strength of an untested "enable_language() is idempotent, safe
# to call from a function" assumption; a scratchpad test that seemed to
# confirm it working had accidentally masked the bug with its own redundant
# top-level enable_language(ASM) call, and only testing the bare
# function-only-call shape (matching how this repo actually uses it) surfaced
# the real failure. Failing clearly at configure time via the check below
# beats silently depending on call order.
#
# VERIFY_SHA256 (optional): also computes the blob's real SHA-256 at BUILD time
# (CMake's own builtin file(SHA256 ...), no extra dependency needed for this
# half) and embeds it as a 32-byte array:
#   extern const unsigned char <prefix>_expected_sha256[32];
# The self-checking loader (include/konative/embed/checked_blob.hpp) re-hashes
# the actual <prefix>_start.._end bytes at RUNTIME (via PicoSHA2) and compares
# against this constant before trusting the blob - turns a corrupted/mismatched
# build artifact into a clear, actionable startup error instead of a mysterious
# crash deep inside a classloader. This is a build-integrity self-check (catches
# "the build pipeline embedded the wrong/truncated bytes"), not a tamper/security
# boundary - the hash is linked in cleartext right next to the data it checks.

include_guard(GLOBAL)

function(konative_embed_binary_blob TARGET_NAME)
  cmake_parse_arguments(ARG "VERIFY_SHA256" "BLOB;SYMBOL" "" ${ARGN})
  if(NOT ARG_BLOB)
    message(FATAL_ERROR "konative_embed_binary_blob(${TARGET_NAME}): BLOB <path> is required")
  endif()
  if(NOT ARG_SYMBOL)
    message(FATAL_ERROR "konative_embed_binary_blob(${TARGET_NAME}): SYMBOL <prefix> is required")
  endif()
  if(NOT TARGET ${TARGET_NAME})
    message(FATAL_ERROR
      "konative_embed_binary_blob(${TARGET_NAME}): no such target - call "
      "add_library()/add_executable() before this function")
  endif()

  # See this file's own top comment: ASM must already be enabled by the
  # including project (Konative's top-level CMakeLists.txt does this) -
  # enable_language(ASM) is deliberately NOT called from here.
  if(NOT CMAKE_ASM_COMPILER)
    message(FATAL_ERROR
      "konative_embed_binary_blob(${TARGET_NAME}): ASM is not enabled for "
      "this project. Call enable_language(ASM) (or add ASM to your "
      "project(... LANGUAGES ...) call) before using this function - see "
      "KonativeEmbedBlob.cmake's top comment for why this isn't done "
      "automatically here.")
  endif()

  # BASE_DIR pinned to the top-level project root, not left to default to
  # CMAKE_CURRENT_SOURCE_DIR - the latter is whichever CMakeLists.txt happens to be calling this
  # function (today, src/platform/android/), an internal implementation detail a user passing a
  # relative -DKONATIVE_EMBEDDED_DEX_PATH=... override has no reason to know or expect (found by a
  # 2026-07-22 code-review pass). An already-absolute ARG_BLOB (the normal, automated-pipeline case)
  # is unaffected either way - ABSOLUTE is a no-op on a path that already is one.
  get_filename_component(ARG_BLOB "${ARG_BLOB}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  # Always forward-slash, even though we're on a Windows host generating for
  # an NDK target: GAS/LLVM's asm string lexer treats backslash as an escape
  # character, so a raw C:\Users\...\ path corrupts silently. See
  # research/incbin_embedding_research.md section 1.
  file(TO_CMAKE_PATH "${ARG_BLOB}" ARG_BLOB)

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/konative_embed/${TARGET_NAME}")
  set(_out_s "${_gen_dir}/${ARG_SYMBOL}_blob.S")
  file(MAKE_DIRECTORY "${_gen_dir}")

  # The generated .S is the OUTPUT of a command that DEPENDS on the blob -
  # portable across every CMake generator (Ninja/Make/Xcode/VS), unlike the
  # OBJECT_DEPENDS source-file-property alternative, which Xcode/VS silently
  # ignore per CMake's own docs. Regenerated at BUILD time (a `cmake -P`
  # script), not via configure_file()/file(GENERATE) at CONFIGURE time -
  # those only rerun when CMake itself reconfigures, the wrong trigger for
  # "only the .dex's bytes changed." See incbin_embedding_research.md section 2.
  add_custom_command(
    OUTPUT "${_out_s}"
    COMMAND "${CMAKE_COMMAND}"
            "-DKONATIVE_BLOB_PATH=${ARG_BLOB}"
            "-DKONATIVE_BLOB_SYMBOL=${ARG_SYMBOL}"
            "-DKONATIVE_OUT_FILE=${_out_s}"
            "-DKONATIVE_VERIFY_SHA256=${ARG_VERIFY_SHA256}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    DEPENDS "${ARG_BLOB}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    COMMENT "konative: regenerating ${ARG_SYMBOL}_blob.S (.incbin ${ARG_BLOB})"
    VERBATIM)

  target_sources(${TARGET_NAME} PRIVATE "${_out_s}")
  set_source_files_properties("${_out_s}" PROPERTIES GENERATED TRUE LANGUAGE ASM)
endfunction()
