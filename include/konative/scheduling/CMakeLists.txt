add_library(konative_scheduling INTERFACE)
add_library(konative::scheduling ALIAS konative_scheduling)

target_sources(konative_scheduling INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../..
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/task_graph.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_pool.hpp
)

target_link_libraries(konative_scheduling INTERFACE
  konative::core
  Taskflow
  bshoshany_thread_pool
  concurrentqueue
  readerwriterqueue
)
