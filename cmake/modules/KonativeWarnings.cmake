# Shared warning/visibility flags for every first-party Konative target.
#
# CMAKE_CXX_VISIBILITY_PRESET=hidden matches ARCHITECTURE.md section 6.3's explicit-exports-only
# stance for the Kotlin/Native <-> C++ boundary: symbol visibility must be controlled
# deliberately on both sides of that boundary, not left to compiler defaults.

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

function(konative_apply_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  endif()
endfunction()
