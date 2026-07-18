# The Kotlin/Native <-> C++ C-ABI boundary - ARCHITECTURE.md \xc2\xa76.3, the single highest-uncertainty
# module in the whole framework. Real, documented, unresolved community friction exists for this
# exact combination (libc++/libm symbol conflicts) - this module is scaffolding for a spike, not a
# proven-working bridge yet.
add_library(konative_interop INTERFACE)
add_library(konative::interop ALIAS konative_interop)

target_sources(konative_interop INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/c_abi_export.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kotlin_native_bridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/symbol_visibility.hpp
)

target_link_libraries(konative_interop INTERFACE konative::core)
