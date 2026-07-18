add_library(konative_core INTERFACE)
add_library(konative::core ALIAS konative_core)

target_sources(konative_core INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/assert.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/non_copyable.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/result.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/type_traits.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/platform.hpp
)

target_compile_features(konative_core INTERFACE cxx_std_20)
# fmt::fmt-header-only, not fmt::fmt (compiled) - log.hpp now links spdlog::spdlog_header_only
# (KonativeDependencies.cmake's SPDLOG_FMT_EXTERNAL_HO option), which itself uses
# fmt::fmt-header-only internally; using the same target here avoids linking two different fmt
# build modes into the same binary (previously dormant only because nothing linked konative_core
# and spdlog together yet - see KonativeDependencies.cmake's own comment on this, now resolved).
target_link_libraries(konative_core INTERFACE fmt::fmt-header-only spdlog::spdlog_header_only)
