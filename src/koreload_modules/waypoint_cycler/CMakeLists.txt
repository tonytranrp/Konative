# A CMake MODULE library (dlopen'd at runtime), never SHARED - mirrors ../pointer_follow/'s own
# CMakeLists.txt exactly (KoReload's own "each hot-reload module = a CMake MODULE library"
# convention, PROMPT.md section 17.1 in the KoReload repo). Links koreload::core and
# konative::app/ecs as compile-time-only dependencies, never a runtime DT_NEEDED shared dependency
# on either - konative::spatial is NOT needed here (unlike pointer_follow), since this module only
# writes PointerFollow::target, it never touches Transform/approach() itself.
add_library(konative_koreload_waypoint_cycler MODULE module.cpp)
target_link_libraries(konative_koreload_waypoint_cycler PRIVATE
  koreload::core konative::app konative::ecs
)
set_target_properties(konative_koreload_waypoint_cycler PROPERTIES
  OUTPUT_NAME "konative_waypoint_cycler"
  PREFIX ""
)
