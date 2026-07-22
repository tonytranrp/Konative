#pragma once

#include <vector>

#include "konative/scheduling/task_graph.hpp"

// A real, permanent self-check that Taskflow's actual thread-spawning/scheduling machinery works on
// whatever target this runs on - matches this framework's own "code checks itself" design principle
// (see include/konative/embed/checked_blob.hpp for the same idea applied to the embedded-dex blob).
// ARCHITECTURE.md section 9 explicitly flags this as the one dependency in this stack with "no
// confirmed track record either way" on Android NDK specifically - every other dependency there has
// cited real precedent; Taskflow didn't, until this actually ran on real hardware (see
// jni_onload.cpp's own call site and embedded_kotlin/README.md-style verification for where that's
// recorded).
namespace konative::scheduling {

// Splits the range [0, task_count*items_per_task) across task_count parallel Taskflow tasks, each
// summing its OWN slice into a distinct vector element - no shared mutable state between tasks
// (each writes exactly one index nothing else touches), so this deliberately does NOT test
// atomics/locking correctness. It tests something narrower and more directly relevant to the real,
// flagged risk: did Taskflow's Executor actually run every task exactly once, whether truly in
// parallel or not, and hand control back correctly afterward - a real scheduling/threading bug (or
// a task silently never running) would show up as a wrong sum, not necessarily a crash.
inline bool run_taskflow_self_check(unsigned task_count = 4, unsigned items_per_task = 10000) {
    TaskGraph flow;
    std::vector<long long> partial_sums(task_count, 0);

    for (unsigned t = 0; t < task_count; ++t) {
        flow.emplace([t, items_per_task, &partial_sums] {
            long long start = static_cast<long long>(t) * static_cast<long long>(items_per_task);
            long long sum = 0;
            for (long long i = start; i < start + static_cast<long long>(items_per_task); ++i) {
                sum += i;
            }
            partial_sums[t] = sum;
        });
    }

    Executor executor;
    executor.run(flow).wait();

    long long total = 0;
    for (long long partial : partial_sums) {
        total += partial;
    }

    long long n = static_cast<long long>(task_count) * static_cast<long long>(items_per_task);
    long long expected = n * (n - 1) / 2; // sum of 0..n-1
    return total == expected;
}

} // namespace konative::scheduling
