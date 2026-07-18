# See render/README.md before adding anything here: this module is deliberately thin. Konative
# does NOT have a hand-written C++ EGL/GLES/Vulkan backend (an earlier skeleton pass did, and it
# was deleted - see ARCHITECTURE.md \xc2\xa76.2). Rendering is owned entirely by Kotlin/Native code in
# native/src/, which cinterop-binds EGL/GLES/Vulkan directly. This module's only job is
# translating konative::events::window::* into calls across the interop boundary
# (konative::interop::konative_render_on_window_created/_destroyed/_tick) - it must never itself
# call an EGL/GLES/Vulkan function.
add_library(konative_render INTERFACE)
add_library(konative::render ALIAS konative_render)

target_sources(konative_render INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer.hpp
)

target_link_libraries(konative_render INTERFACE konative::core konative::events konative::interop)
