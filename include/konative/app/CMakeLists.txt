add_library(konative_app INTERFACE)
add_library(konative::app ALIAS konative_app)

target_sources(konative_app INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/application.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_config.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/entry_point.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/lifecycle_bridge.hpp
)

target_link_libraries(konative_app INTERFACE konative::core konative::ecs konative::events)
