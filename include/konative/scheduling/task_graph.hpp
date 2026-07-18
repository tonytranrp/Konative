#pragma once

#include <taskflow/taskflow.hpp>

// Default scheduler for anything with real cross-system dependencies (ARCHITECTURE.md section 1/section 4).
// Deliberately a thin alias, not a wrapping abstraction - Taskflow's own API (tf::Taskflow,
// tf::Executor, tf::Subflow) is already the right shape; don't hide it behind indirection.
namespace konative::scheduling {

using TaskGraph = tf::Taskflow;
using Executor = tf::Executor;

} // namespace konative::scheduling
