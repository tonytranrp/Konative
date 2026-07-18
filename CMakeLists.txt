cmake_minimum_required(VERSION 3.23)

project(Konative
  VERSION 0.1.0
  DESCRIPTION "Kotlin (Jetpack Compose) + C++ fused into one native Android .so via embedded-dex, corrosion-style CMake tooling"
  LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

option(KONATIVE_BUILD_EXAMPLES "Build examples/" ${PROJECT_IS_TOP_LEVEL})
option(KONATIVE_BUILD_TESTS "Build tests/" ${PROJECT_IS_TOP_LEVEL})
option(KONATIVE_BUILD_KOTLIN_NATIVE "Compile and link the Kotlin/Native side (native/) - see ARCHITECTURE.md section 6.3 for the linking risk this carries" ON)

# See ARCHITECTURE.md section 4 - CPM.cmake is vendored in-tree (cmake/CPM.cmake), never
# downloaded fresh at configure time, so that a cold clone + a warm CPM_SOURCE_CACHE can
# reconfigure fully offline.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CPM.cmake)

include(KonativeWarnings)
if(ANDROID)
  include(KonativeAndroidToolchain)
  include(KonativeKotlinNative)
  include(KonativeEmbedBlob)
  # Required for cmake/modules/KonativeEmbedBlob.cmake's .incbin-based blob embedder - the NDK's
  # own android.toolchain.cmake sets up no ASM compiler variables at all (verified directly
  # against its source, research/incbin_embedding_research.md section 3), and calling
  # enable_language(ASM) for the first time from inside a CMake function (rather than here, a
  # top-level CMakeLists.txt) fails outright - see KonativeEmbedBlob.cmake's own top comment for
  # the real, reproduced failure this works around.
  enable_language(ASM)
endif()
include(KonativeDependencies)

add_subdirectory(include/konative)
add_subdirectory(src)

if(ANDROID AND KONATIVE_BUILD_KOTLIN_NATIVE)
  add_subdirectory(native)
endif()

if(KONATIVE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(KONATIVE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
