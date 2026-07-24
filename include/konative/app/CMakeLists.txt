add_library(konative_app INTERFACE)
add_library(konative::app ALIAS konative_app)

target_sources(konative_app INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/application.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app_config.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/json_config_file.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/entry_point.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/lifecycle_bridge.hpp
)

# konative::reflect: config/json_config_file.hpp reads/writes component structs through the
# entt::meta + Glaze pairing (meta_glaze_json.hpp) - a real dependency of this module now, not a
# jni_onload.cpp-only concern.
target_link_libraries(konative_app INTERFACE konative::core konative::ecs konative::events konative::reflect)
