# include/konative/scheduling/

Concurrency primitives layered on top of the single-threaded `ecs::SystemGraph`/`events::Dispatcher`
core: Taskflow-based DAG scheduling, a lightweight thread-pool fallback, and the lock-free
queue types used for cross-thread event/job posting.

## Hard rules

- **`entt::registry` and `entt::dispatcher` are not thread-safe — this module exists so nothing
  else has to pretend otherwise.** The sanctioned pattern for parallel ECS work
  (`ARCHITECTURE.md` §5, straight from EnTT's own maintainer) is: get a view's splittable index
  range via `view.handle()`, partition it with `detail/view_split.hpp`'s `split_evenly()`, hand
  each slice to a `TaskGraph`/`ThreadPool` job that does `.get<Components...>()` by index. Never
  call directly into a live `Registry`/`Dispatcher` from more than one thread concurrently without
  going through this module's boundary primitives first.
- **Pick ONE scheduler per subsystem — `TaskGraph` (Taskflow) or `ThreadPool` (BS::thread_pool),
  never both for the same work.** Use `TaskGraph` when there's a real dependency graph between
  jobs (one system must finish before another starts); use `ThreadPool` for simple
  fire-and-wait-for-all parallelism. Running two competing schedulers for one subsystem is a
  correctness and a mental-overhead risk, not a performance win.
- **Cross-thread event posting always goes through `concurrentqueue`/`readerwriterqueue`, never
  directly into `events::Dispatcher`.** Worker threads `enqueue()` onto the lock-free queue;
  exactly one thread (the frame thread, from `World::tick()`) drains it into
  `dispatcher.enqueue<E>()` before calling `dispatcher.update()`. This is what keeps
  `events::Dispatcher` single-threaded-safe by construction — don't bypass it "just this once."
- **This module does not know about EGL/GLES/Vulkan, EnTT `meta`, or Kotlin/Native.** It's pure
  concurrency infrastructure — a job/task here should be describable as "run this callable, maybe
  in parallel, maybe later," nothing more domain-specific.

## Adding to this folder

A new file here should be a new concurrency *primitive* (a new queue wrapper, a new scheduling
helper), not a specific parallel system's implementation — the system itself (and its use of
`TaskGraph`/`ThreadPool`) belongs wherever that system's other logic lives.
