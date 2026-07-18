# The smallest possible Konative app: one Application subclass, one triangle rendered through
# whichever backend is linked in. Desktop-buildable (no Android target required) so the ECS/events
# core can be exercised without a device - the render backend is a no-op off-Android.
add_executable(konative_minimal_triangle main.cpp)
target_link_libraries(konative_minimal_triangle PRIVATE konative::konative)
konative_apply_warnings(konative_minimal_triangle)
