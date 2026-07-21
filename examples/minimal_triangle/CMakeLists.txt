# The smallest possible Konative app: one Application subclass, no rendering (see main.cpp's own
# comment and examples/README.md - despite the folder name, this doesn't render a triangle, it's the
# bare lifecycle skeleton). Desktop-buildable (no Android target required) so the ECS/events/app
# core can be exercised without a device.
add_executable(konative_minimal_triangle main.cpp)
target_link_libraries(konative_minimal_triangle PRIVATE konative::konative)
konative_apply_warnings(konative_minimal_triangle)
