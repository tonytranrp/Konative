add_library(konative_jni INTERFACE)
add_library(konative::jni ALIAS konative_jni)

target_sources(konative_jni INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ref.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/call.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dex_loader.hpp
)

target_link_libraries(konative_jni INTERFACE konative::core)
