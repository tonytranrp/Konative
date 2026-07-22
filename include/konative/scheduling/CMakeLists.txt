add_library(konative_scheduling INTERFACE)
add_library(konative::scheduling ALIAS konative_scheduling)

target_sources(konative_scheduling INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/task_graph.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cross_thread_event_queue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/taskflow_self_check.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool_self_check.hpp
)

target_link_libraries(konative_scheduling INTERFACE
  konative::core
  konative::events
  Taskflow
  bshoshany_thread_pool
  concurrentqueue
  readerwriterqueue
)
