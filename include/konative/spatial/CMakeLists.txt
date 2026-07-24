add_library(konative_spatial INTERFACE)
add_library(konative::spatial ALIAS konative_spatial)

target_sources(konative_spatial INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/transform.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/transform_self_check.hpp
)

target_link_libraries(konative_spatial INTERFACE konative::ecs glm::glm)
