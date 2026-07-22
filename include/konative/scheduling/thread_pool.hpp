#pragma once

#include <BS_thread_pool.hpp>

// Lightweight fallback scheduler for subsystems that don't need Taskflow's DAG model
// (ARCHITECTURE.md section 4) - pick ONE of TaskGraph or ThreadPool per subsystem, don't run both as
// competing schedulers for the same work.
namespace konative::scheduling {

// BS::thread_pool (v4.1.0, the pinned tag) is a plain, non-template class, NOT BS::thread_pool<T> -
// `BS::thread_pool<>` doesn't compile ("expected ';' after alias declaration"). A real bug in this
// exact line, undetected until scheduling/thread_pool_self_check.hpp became the first thing to ever
// actually #include and compile this header - nothing had exercised ThreadPool at all before that.
using ThreadPool = BS::thread_pool;

} // namespace konative::scheduling
