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
target_link_libraries(konative_core INTERFACE fmt::fmt)
