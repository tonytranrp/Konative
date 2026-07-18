add_library(konative_reflect INTERFACE)
add_library(konative::reflect ALIAS konative_reflect)

target_sources(konative_reflect INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/component_traits.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/meta_registry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/registration_thunk.hpp
)

target_link_libraries(konative_reflect INTERFACE konative::core EnTT::EnTT)
