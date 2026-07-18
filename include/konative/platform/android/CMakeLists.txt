add_library(konative_platform_android INTERFACE)
add_library(konative::platform::android ALIAS konative_platform_android)

target_sources(konative_platform_android INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/activity_bridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/looper_pump.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/native_app_glue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/app_cmd_translation.hpp
)

target_link_libraries(konative_platform_android INTERFACE
  konative::core
  konative::app
  konative::events
  android
  log
)
