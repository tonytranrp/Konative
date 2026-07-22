#pragma once

#include <vector>

#include "konative/scheduling/thread_pool.hpp"

// A real, permanent self-check for BS::thread_pool (ThreadPool) - the same "no confirmed track
// record either way" question Taskflow had before its own taskflow_self_check.hpp, and
// BS::thread_pool has had zero test coverage or real usage anywhere in this codebase until now
// (confirmed by repo-wide grep before writing this). Same "code checks itself" precedent.
namespace konative::scheduling {

// Splits a range across real ThreadPool blocks (BS::thread_pool's own submit_blocks(), the "simple
// fire-and-wait-for-all parallelism" idiom scheduling/README.md's Hard Rule reserves ThreadPool
// for), each summing its own slice, then verifies the combined result against the
// mathematically-correct sum of 0..total_items-1 - not just "didn't crash."
inline bool run_thread_pool_self_check(unsigned task_count = 4, unsigned items_per_task = 10000) {
    const unsigned total_items = task_count * items_per_task;
    ThreadPool pool(task_count);

    auto future = pool.submit_blocks(
        0U, total_items,
        [](unsigned start, unsigned end) -> unsigned long long {
            unsigned long long sum = 0;
            for (unsigned i = start; i < end; ++i) {
                sum += i;
            }
            return sum;
        },
        task_count);

    const std::vector<unsigned long long> partial_sums = future.get();

    unsigned long long total = 0;
    for (unsigned long long partial : partial_sums) {
        total += partial;
    }

    const unsigned long long expected =
        static_cast<unsigned long long>(total_items - 1) * total_items / 2;
    return total == expected;
}

} // namespace konative::scheduling
