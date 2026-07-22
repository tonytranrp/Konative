# Every third-party dependency Konative fetches, via the vendored cmake/CPM.cmake (ARCHITECTURE.md
# section 4). Every GIT_TAG below is an immutable tag, never a branch - CPM's own issue tracker confirms
# offline/reproducible builds silently break against a moving branch ref even with a warm
# CPM_SOURCE_CACHE. Bump tags deliberately, one at a time, like any other pinned dependency.

# --- EnTT: reflection (entt::meta) + ECS (entt::registry) + events (entt::dispatcher) ---
# DOWNLOAD_ONLY: EnTT's own CMakeLists.txt configures ITS test/doc/packaging targets, not
# consumption as a dependency - CPM's own official EnTT example does exactly this.
CPMAddPackage(
  NAME EnTT
  GITHUB_REPOSITORY skypjack/entt
  GIT_TAG v3.13.2
  DOWNLOAD_ONLY YES
)
if(EnTT_ADDED AND NOT TARGET EnTT)
  add_library(EnTT INTERFACE)
  add_library(EnTT::EnTT ALIAS EnTT)
  target_include_directories(EnTT SYSTEM INTERFACE "${EnTT_SOURCE_DIR}/src")
  target_compile_features(EnTT INTERFACE cxx_std_20)
endif()

# --- Boost.PFR: aggregate reflection (field count/values/names), for entt::meta auto-registration ---
# DOWNLOAD_ONLY, same reason as EnTT above: PFR's own CMakeLists.txt (verified directly, 2026-07-22)
# is written for the Boost superproject build (project(... VERSION "${BOOST_SUPERPROJECT_VERSION}")),
# not standalone consumption - that variable is never set outside the full boostorg/boost monorepo,
# so letting CPM configure it directly would be a real, avoidable risk for a header-only library
# that needs nothing from its own CMakeLists.txt. boost-1.91.0, not a newer beta tag (boostorg/pfr's
# own tag history has beta-suffixed releases past this) - matching this project's own established
# "pin a real stable release, not the newest tag" convention (see glaze's tag-history comment below).
CPMAddPackage(
  NAME pfr
  GITHUB_REPOSITORY boostorg/pfr
  GIT_TAG boost-1.91.0
  DOWNLOAD_ONLY YES
)
if(pfr_ADDED AND NOT TARGET boost_pfr)
  add_library(boost_pfr INTERFACE)
  add_library(Boost::pfr ALIAS boost_pfr)
  target_include_directories(boost_pfr SYSTEM INTERFACE "${pfr_SOURCE_DIR}/include")
  target_compile_features(boost_pfr INTERFACE cxx_std_20)
endif()

# --- GLM: math (vectors/matrices) ---
CPMAddPackage(
  NAME glm
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.3
)

# --- fmt + spdlog: logging (header-only mode; spdlog ships a first-class Android logcat sink) ---
# Pinned to current releases, not the versions originally drafted (10.2.1/v1.14.1) - that pair
# hard-failed on this machine's Clang 22.1.4 (a genuinely very new/bleeding-edge llvm-mingw build):
# fmt 10.2.1's consteval-based compile-time format-string checking (FMT_STRING/basic_format_string)
# rejected as "not a constant expression" under this compiler, inside spdlog's own bundled
# SPDLOG_FMT_STRING usages. Verified fixed by bumping both to current releases.
CPMAddPackage(
  NAME fmt
  GITHUB_REPOSITORY fmtlib/fmt
  GIT_TAG 12.2.0
)
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  GIT_TAG v1.17.0
  # SPDLOG_FMT_EXTERNAL_HO, not the similarly-named SPDLOG_FMT_EXTERNAL - this is spdlog's own real
  # option (spdlog/CMakeLists.txt, "Use external fmt header-only library instead of bundled"), not
  # a typo, and it is ARCHITECTURE.md section 4's own explicitly-named choice (see the spdlog/fmt
  # table row there, which names this exact option string as the deliberate header-only setting).
  # A prior review pass flagged this as a suspected unreviewed typo/regression by diffing against
  # an earlier commit that had plain SPDLOG_FMT_EXTERNAL - that diff was real, but the conclusion
  # was wrong: re-verified directly against the fetched spdlog v1.17.0 source
  # (build/desktop-debug/_deps/spdlog-src/CMakeLists.txt) that SPDLOG_FMT_EXTERNAL_HO is a distinct,
  # real, valid option (mutually exclusive with SPDLOG_FMT_EXTERNAL, not interchangeable with it) -
  # setting it links spdlog against fmt::fmt-header-only instead of fmt::fmt. Do not "fix" this back
  # to SPDLOG_FMT_EXTERNAL without re-reading this comment and ARCHITECTURE.md section 4 first.
  #
  # RESOLVED (this comment used to flag it as a real, still-open item - stale since
  # include/konative/core/CMakeLists.txt's own comment says so directly): konative_core links
  # fmt::fmt-header-only (not fmt::fmt compiled) for the same reason, avoiding two different fmt
  # build modes in the same binary now that log.hpp does link spdlog::spdlog_header_only for real
  # (android_logger_mt()/stdout_color_sinks, not a TODO anymore).
  OPTIONS "SPDLOG_FMT_EXTERNAL_HO ON"
)

# --- Taskflow: DAG-based job scheduling for ECS systems ---
CPMAddPackage(
  NAME Taskflow
  GITHUB_REPOSITORY taskflow/taskflow
  GIT_TAG v3.7.0
  OPTIONS "TF_BUILD_TESTS OFF" "TF_BUILD_EXAMPLES OFF"
)

# --- BS::thread_pool: lightweight fallback scheduler for subsystems that don't need a DAG ---
CPMAddPackage(
  NAME bshoshany_thread_pool
  GITHUB_REPOSITORY bshoshany/thread-pool
  GIT_TAG v4.1.0
  DOWNLOAD_ONLY YES
)
if(bshoshany_thread_pool_ADDED AND NOT TARGET bshoshany_thread_pool)
  add_library(bshoshany_thread_pool INTERFACE)
  target_include_directories(bshoshany_thread_pool SYSTEM INTERFACE "${bshoshany_thread_pool_SOURCE_DIR}/include")
endif()

# --- concurrentqueue / readerwriterqueue: lock-free cross-thread event/job posting ---
CPMAddPackage(
  NAME concurrentqueue
  GITHUB_REPOSITORY cameron314/concurrentqueue
  GIT_TAG v1.0.4
)
CPMAddPackage(
  NAME readerwriterqueue
  GITHUB_REPOSITORY cameron314/readerwriterqueue
  GIT_TAG v1.0.6
)

# --- libcoro: C++20 coroutines ---
# CORRECTION (2026-07-22, a real empirical spike building against android-arm64): this was chosen as
# "the one concurrency lib with documented Android NDK support," but that claim is only true
# upstream in general, not for this pinned v0.16.0 tag against Android NDK r28's specific libc++
# build - condition_variable.cpp/scheduler.cpp/thread_pool.cpp need std::jthread/std::stop_token
# unconditionally, and NDK r28's libc++ has no working implementation of either (confirmed via the
# __cpp_lib_jthread feature-test macro being absent under <version>, identically at API 26 and API
# 30 - not an API-level gate, genuinely unimplemented here). See
# include/konative/events/CMakeLists.txt (libcoro is linked `if(NOT ANDROID)` only) and
# ARCHITECTURE.md section 9 for the full writeup. coro::task/coro::event (konative::events'
# NextEventAwaiter) don't themselves need stop_token, but libcoro compiles as one fixed-source-list
# static library target, so the whole thing fails to build for Android regardless.
# GIT_SUBMODULES "" is an attempt to skip the vendor/c-ares/c-ares submodule (only needed for
# LIBCORO_FEATURE_NETWORKING, which is OFF below) - CORRECTION (a code review empirically
# disproved the original claim here): this does NOT actually skip the submodule. CMake's CMP0097
# policy defaults to OLD behavior (never explicitly set to NEW anywhere in this repo), under which
# an empty GIT_SUBMODULES string is treated as "unset" rather than "fetch none," so the ~559-file
# c-ares submodule is still checked out in full regardless of this line. It is harmless (unused,
# just wasted clone time/disk), NOT the fix for the real git-for-Windows submodule-update failure
# ("fatal: not a git repository: '.git'") this project hit - that failure was actually resolved by
# the -DGIT_EXECUTABLE workaround (see BUILDING.md), which fixes git invocation generally, not
# this option specifically. Left in place as a statement of intent + a real TODO (setting CMP0097
# NEW correctly, likely needs to happen before FetchContent's internal subbuild project is
# generated, not just here) rather than removed, since it's not actively harmful.
CPMAddPackage(
  NAME libcoro
  GITHUB_REPOSITORY jbaldwin/libcoro
  GIT_TAG v0.16.0
  GIT_SUBMODULES ""
  OPTIONS "LIBCORO_FEATURE_NETWORKING OFF" "LIBCORO_FEATURE_TLS OFF" "LIBCORO_BUILD_TESTS OFF"
          "LIBCORO_BUILD_EXAMPLES OFF"
)

# --- PicoSHA2: single-header SHA-256, for the self-checking embedded-blob verifier
# (include/konative/embed/checked_blob.hpp) - runtime counterpart to
# konative_embed_binary_blob()'s VERIFY_SHA256 option (cmake/modules/KonativeEmbedBlob.cmake),
# which computes the expected hash at build time via CMake's own builtin file(SHA256 ...). Tag
# verified via `git ls-remote --tags` per this file's own hard rule (ARCHITECTURE.md section 4).
# DOWNLOAD_ONLY: it's one header, no CMakeLists.txt of its own to speak of.
CPMAddPackage(
  NAME PicoSHA2
  GITHUB_REPOSITORY okdshin/PicoSHA2
  GIT_TAG v1.0.1
  DOWNLOAD_ONLY YES
)
if(PicoSHA2_ADDED AND NOT TARGET PicoSHA2)
  add_library(PicoSHA2 INTERFACE)
  add_library(PicoSHA2::PicoSHA2 ALIAS PicoSHA2)
  target_include_directories(PicoSHA2 SYSTEM INTERFACE "${PicoSHA2_SOURCE_DIR}")
endif()

# --- Glaze: macro-free reflection-driven JSON, for config/hot-reload ---
# v3.5.4 never existed (verified: `git ls-remote --tags` against the real repo) - the actual
# tag history jumps from the 3.x-2.x lines this project's own earlier research cited straight to
# the 7.x line current as of this pin. Re-verify this tag exists before ever bumping it.
CPMAddPackage(
  NAME glaze
  GITHUB_REPOSITORY stephenberry/glaze
  GIT_TAG v7.9.1
)

# --- cereal: binary save-state snapshots, paired with EnTT's native snapshot API ---
CPMAddPackage(
  NAME cereal
  GITHUB_REPOSITORY USCiLab/cereal
  GIT_TAG v1.3.2
  OPTIONS "JUST_INSTALL_CEREAL ON"
)

# --- doctest: testing, chosen over Catch2 for near-zero compile-time overhead (ARCHITECTURE.md section 2) ---
if(KONATIVE_BUILD_TESTS)
  # doctest v2.4.11's own CMakeLists.txt declares cmake_minimum_required(VERSION 2.8...) - a floor
  # CMake 4.x hard-refuses to configure at all (not just a deprecation warning) since CMake 4.0
  # dropped support for pre-3.5 projects entirely. CMake's own docs are explicit that
  # CMAKE_POLICY_VERSION_MINIMUM "should not be set by a project... as a way to set its own policy
  # version" globally - a code review caught this repo doing exactly that (in every
  # CMakePresets.json preset, affecting every dependency, not just the one that needs it). Scope
  # it narrowly instead: a plain (non-cache) set() is visible to add_subdirectory()-included
  # children in the same directory-variable-scope chain, so this affects only doctest's own
  # configure, not concurrentqueue/readerwriterqueue/etc. (which only warn, not hard-fail, on
  # their own older floors, and shouldn't have this silently lowered for them too).
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.4.11
  )
  unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()
