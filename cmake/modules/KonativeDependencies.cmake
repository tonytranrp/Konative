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
  OPTIONS "SPDLOG_FMT_EXTERNAL ON"
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

# --- libcoro: C++20 coroutines - the one concurrency lib with documented Android NDK support ---
# GIT_SUBMODULES "" - libcoro's vendor/c-ares/c-ares submodule is only needed for
# LIBCORO_FEATURE_NETWORKING, which is OFF below; skipping it also sidesteps a real, reproduced
# git-for-Windows submodule-update failure ("fatal: not a git repository: '.git'") this project
# hit fetching it on a real Windows dev machine.
CPMAddPackage(
  NAME libcoro
  GITHUB_REPOSITORY jbaldwin/libcoro
  GIT_TAG v0.16.0
  GIT_SUBMODULES ""
  OPTIONS "LIBCORO_FEATURE_NETWORKING OFF" "LIBCORO_FEATURE_TLS OFF" "LIBCORO_BUILD_TESTS OFF"
)

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
  CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.4.11
  )
endif()
