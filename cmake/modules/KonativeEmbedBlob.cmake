# konative_embed_binary_blob(<target> BLOB <path> SYMBOL <prefix>)
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

include_guard(GLOBAL)

function(konative_embed_binary_blob TARGET_NAME)
  cmake_parse_arguments(ARG "" "BLOB;SYMBOL" "" ${ARGN})
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

  # The NDK's own android.toolchain.cmake sets up NO ASM compiler variables at
  # all (verified directly against its source) - every consumer must enable
  # this itself. enable_language() is idempotent - safe to call from a
  # function, safe if called more than once across multiple blobs.
  enable_language(ASM)

  get_filename_component(ARG_BLOB "${ARG_BLOB}" ABSOLUTE)
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
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    DEPENDS "${ARG_BLOB}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    COMMENT "konative: regenerating ${ARG_SYMBOL}_blob.S (.incbin ${ARG_BLOB})"
    VERBATIM)

  target_sources(${TARGET_NAME} PRIVATE "${_out_s}")
  set_source_files_properties("${_out_s}" PROPERTIES GENERATED TRUE LANGUAGE ASM)
endfunction()
