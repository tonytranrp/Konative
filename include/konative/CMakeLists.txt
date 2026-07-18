# Konative is header-only-first (ARCHITECTURE.md section 2): one INTERFACE library per module folder,
# each declaring its own headers via FILE_SET HEADERS (never file(GLOB_RECURSE ...) - CMake's own
# docs warn against it, and it's a poor fit for the events/ folder specifically, which gains new
# files constantly).

add_subdirectory(core)
add_subdirectory(reflect)
add_subdirectory(ecs)
add_subdirectory(events)
add_subdirectory(scheduling)
add_subdirectory(interop)
add_subdirectory(app)
if(ANDROID)
  add_subdirectory(platform/android)
endif()
add_subdirectory(render)

add_library(konative INTERFACE)
add_library(konative::konative ALIAS konative)
target_link_libraries(konative INTERFACE
  konative::core
  konative::reflect
  konative::ecs
  konative::events
  konative::scheduling
  konative::interop
  konative::app
  konative::render
)
