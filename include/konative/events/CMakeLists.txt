# events/ - ONE .hpp per event type, organized into feature-area subfolders (lifecycle/, window/,
# input/). dispatcher.hpp is the single shared entt::dispatcher wrapper - it is NOT an event
# itself. This split (one generic dispatcher, many small single-purpose event types) is the
# dominant real-world pattern (Hazel, EnTT's own dispatcher, eventpp, tinyevents) - see
# ARCHITECTURE.md section 2. Never add a second event type to an existing header - a new event always
# means a new file.
add_library(konative_events INTERFACE)
add_library(konative::events ALIAS konative_events)

target_sources(konative_events INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/dispatcher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle/AppStartedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle/AppPausedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle/AppResumedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lifecycle/AppDestroyedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/window/WindowCreatedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/window/WindowDestroyedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/window/WindowResizedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/window/WindowFocusChangedEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input/TouchDownEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input/TouchMoveEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input/TouchUpEvent.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/input/KeyEvent.hpp
)

target_link_libraries(konative_events INTERFACE konative::core EnTT::EnTT)
