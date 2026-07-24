add_executable(konative_tests
  main.cpp
  test_result.cpp
  test_events.cpp
  test_reflect.cpp
  test_checked_blob.cpp
  test_log.cpp
  test_app.cpp
  test_cross_thread_event_queue.cpp
  test_taskflow_self_check.cpp
  test_next_event_awaiter.cpp
  test_registry_snapshot.cpp
  test_thread_pool_self_check.cpp
  test_glm_storage.cpp
  test_world.cpp
  test_spsc_event_queue.cpp
  test_pfr_auto_registration.cpp
  test_meta_glaze_json.cpp
  test_app_config.cpp
  test_json_config_file.cpp
  test_transform.cpp
)
target_link_libraries(konative_tests PRIVATE konative::konative doctest::doctest)
konative_apply_warnings(konative_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(konative_tests)
