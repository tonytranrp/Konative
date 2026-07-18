add_executable(konative_tests
  test_result.cpp
  test_events.cpp
)
target_link_libraries(konative_tests PRIVATE konative::konative doctest::doctest)
konative_apply_warnings(konative_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(konative_tests)
