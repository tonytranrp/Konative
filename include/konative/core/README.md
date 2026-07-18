# include/konative/core/

Foundational, dependency-free utilities every other module builds on: `Result<T,E>`, logging,
assertion, `NonCopyable`, and the shared C++20 concepts (`konative::core::EventType`, etc.) used
across the rest of the codebase.

## Hard rules

- **Zero dependencies on any other `konative::*` module.** `core/` sits at the bottom of the
  dependency graph — if a header here needs to `#include "konative/ecs/..."` or
  `"konative/events/..."`, that logic belongs in a higher module, not here. The only third-party
  dependency `core/` may link is `fmt` (for `log.hpp`).
- **No platform-specific code beyond `detail/platform.hpp`'s compile-time detection macros.**
  Anything that needs `#ifdef __ANDROID__` branching belongs in `platform/android/`, not `core/`.
- **`Result<T,E>`, not exceptions, at any boundary that might cross the Kotlin/Native interop
  edge.** An unwound C++ exception cannot safely cross a plain-C-ABI call
  (`ARCHITECTURE.md` §6.3) — `core/result.hpp` exists specifically so error handling has an
  exception-free option available everywhere in the codebase, not just at the interop boundary.
- **`detail/` holds anything that can't be hidden behind a `.cpp` boundary** (macros, compile-time
  platform detection) — never expose it as part of the public API surface.

## Adding to this folder

A new file here should be a genuinely foundational utility with no dependents' assumptions baked
in (i.e., it would make sense in *any* C++ project, not just Konative-shaped ones). If what you're
adding is EnTT-specific, it belongs in `reflect/` or `ecs/` instead.
