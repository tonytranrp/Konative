#pragma once

#include <BS_thread_pool.hpp>

// Lightweight fallback scheduler for subsystems that don't need Taskflow's DAG model
// (ARCHITECTURE.md \xc2\xa74) - pick ONE of TaskGraph or ThreadPool per subsystem, don't run both as
// competing schedulers for the same work.
namespace konative::scheduling {

using ThreadPool = BS::thread_pool<>;

} // namespace konative::scheduling
