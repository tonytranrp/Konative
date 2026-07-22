#include <doctest/doctest.h>

#include "konative/scheduling/thread_pool_self_check.hpp"

TEST_CASE("run_thread_pool_self_check: default parameters produce the mathematically correct sum") {
    CHECK(konative::scheduling::run_thread_pool_self_check());
}

TEST_CASE("run_thread_pool_self_check: a single task (no real parallelism) still computes correctly") {
    CHECK(konative::scheduling::run_thread_pool_self_check(1, 1000));
}

TEST_CASE("run_thread_pool_self_check: many small tasks still computes correctly") {
    CHECK(konative::scheduling::run_thread_pool_self_check(64, 500));
}
