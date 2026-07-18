add_library(konative_embed INTERFACE)
add_library(konative::embed ALIAS konative_embed)

target_sources(konative_embed INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/checked_blob.hpp
)

target_link_libraries(konative_embed INTERFACE konative::core PicoSHA2::PicoSHA2)
