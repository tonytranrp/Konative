# KONATIVE_ENABLE_KORELOAD only (see root CMakeLists.txt) - PROMPT.md section 13 M8 in the
# KoReload repo: converting PointerFollow's system into a real, hot-reloadable KoReload module
# instead of the always-linked static registration in src/platform/android/jni_onload.cpp.

# KoReload is a separate, sibling repo (github.com/tonytranrp/Konative's own companion project),
# not vendored into this tree - added as a real CMake subdirectory (not CPMAddPackage-from-a-URL)
# so local KoReload edits are picked up immediately without a re-fetch, matching how this whole
# integration is meant to be iterated on during development. KORELOAD_SOURCE_DIR defaults to the
# sibling-directory layout both repos actually live in on this machine; override via
# CMakeUserPresets.json's cacheVariables if a checkout ever lives somewhere else.
set(KORELOAD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../KoReload," CACHE PATH
  "Path to the KoReload repo checkout")

if(NOT EXISTS "${KORELOAD_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "KONATIVE_ENABLE_KORELOAD is ON but KORELOAD_SOURCE_DIR (${KORELOAD_SOURCE_DIR}) doesn't "
    "contain a CMakeLists.txt - set -DKORELOAD_SOURCE_DIR=<path> explicitly if the KoReload "
    "checkout isn't the default sibling directory.")
endif()

# We only need koreload::core (the registry/crash-guard/interface-handle static library) here -
# suppress KoReload's own examples/tests so this opt-in path stays fast and never pulls
# tools/koreload_cli's desktop-only deps (efsw/reproc/CLI11/nlohmann_json) into an Android build.
# KoReload's own root CMakeLists.txt already gates koreload_cli behind `if(NOT ANDROID)` and its
# spikes behind `KORELOAD_BUILD_SPIKES` (default OFF), so those two need no action here.
set(KORELOAD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KORELOAD_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# KoReload's own root CMakeLists.txt unconditionally CPMAddPackage()s doctest (not gated behind
# KORELOAD_BUILD_TESTS), and doctest v2.4.11's own cmake_minimum_required() floor is below what
# CMake 4.x still supports - the exact same issue KonativeDependencies.cmake's own doctest fetch
# already has this identical narrow fix for (see that file's own comment for the full reasoning,
# including the earlier code-review finding that setting this globally in every CMakePresets.json
# preset - which this integration briefly, incorrectly did too - silently lowers the floor for
# every dependency, not just the one that needs it). A plain (non-cache) set() here is visible to
# this add_subdirectory() call's own children in the same directory-variable-scope chain, so it
# affects only KoReload's own configure, not anything else in this build.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(${KORELOAD_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/koreload_core_build)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_subdirectory(pointer_follow)
