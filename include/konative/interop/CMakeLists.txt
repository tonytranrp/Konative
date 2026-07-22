# The Kotlin/Native <-> C++ C-ABI boundary - ARCHITECTURE.md section 6.3. No longer on this
# project's critical path now that rendering is JVM-hosted Compose, dex-embedded and loaded via
# JNI_OnLoad (embedded_kotlin/, include/konative/jni/) - that chain was itself once flagged as the
# framework's largest concentration of unproven risk (ARCHITECTURE.md section 9), but has since
# been landed and repeatedly re-verified on real hardware. Kept because its C-ABI mechanics (real,
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
