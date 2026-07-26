# A CMake MODULE library (dlopen'd at runtime), never SHARED - KoReload's own
# "each hot-reload module = a CMake MODULE library" convention (PROMPT.md section 17.1 in the
# KoReload repo). Links koreload::core and konative::app/ecs/spatial as compile-time-only
# dependencies (INTERFACE libraries throughout this codebase already - see each module's own
# CMakeLists.txt), never a runtime DT_NEEDED shared dependency on either.
add_library(konative_koreload_pointer_follow MODULE module.cpp)
target_link_libraries(konative_koreload_pointer_follow PRIVATE
  koreload::core konative::app konative::ecs konative::spatial
)
set_target_properties(konative_koreload_pointer_follow PROPERTIES
  OUTPUT_NAME "konative_pointer_follow"
  PREFIX ""
)
