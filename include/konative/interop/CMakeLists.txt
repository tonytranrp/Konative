# The Kotlin/Native <-> C++ C-ABI boundary - ARCHITECTURE.md section 6.3. No longer on this
# project's critical path now that rendering is JVM-hosted Compose (ARCHITECTURE.md section 9) -
# the single largest concentration of unproven risk has moved to the JNI_OnLoad-to-Compose chain
# instead (embedded_kotlin/, include/konative/jni/). Kept because its C-ABI mechanics (real,
# documented, unresolved community friction for this exact libc++/libm symbol-conflict combination)
# remain real and useful for a possible future, non-rendering use of Kotlin/Native.
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
