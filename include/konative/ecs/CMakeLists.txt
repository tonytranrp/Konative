add_library(konative_ecs INTERFACE)
add_library(konative::ecs ALIAS konative_ecs)

target_sources(konative_ecs INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/registry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/system.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/world.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/system_traits.hpp
)

target_link_libraries(konative_ecs INTERFACE konative::core konative::reflect konative::events EnTT::EnTT)
