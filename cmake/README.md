# cmake/

The vendored `CPM.cmake` (offline-reproducible dependency fetching — `ARCHITECTURE.md` §4) and
every Konative-authored CMake module (`modules/`).

## Hard rules

- **`CPM.cmake` is vendored, not downloaded fresh at configure time — never change this.** CPM's
  own documentation confirms the "download `get_cpm.cmake` at configure time" pattern cannot
  deliver offline reproducibility even with a warm `CPM_SOURCE_CACHE` (`ARCHITECTURE.md` §4). To
  upgrade CPM itself, re-run the one-line download command and commit the result — never replace
  the vendored file with a `file(DOWNLOAD ...)` bootstrap.
- **Every `GIT_TAG` in `modules/KonativeDependencies.cmake` is an immutable tag or commit hash,
  never a branch name.** CPM's own issue tracker confirms offline mode silently breaks against a
  moving branch ref even with a populated source cache.
- **`KonativeAndroidToolchain.cmake` reads the NDK toolchain file's own already-established cache
  variables (`ANDROID_ABI`) — it never re-derives ABI/API-level mapping independently.** If a new
  ABI needs support, add one more branch to its `if/elseif` chain, matching the NDK's own naming,
  not a parallel detection mechanism.
- **`KonativeKotlinNative.cmake` delegates to the real `kotlinc-native` binary via
  `add_custom_command`** — it must never reimplement Kotlin/Native compilation semantics itself
  (`ARCHITECTURE.md` §7's whole point, mirroring corrosion's own design philosophy for wrapping
  `cargo`).

## Adding to this folder

A new module here should be a new `Konative*.cmake` file exposing one clear CMake function (or a
small, related set) — following `KonativeKotlinNative.cmake`'s existing shape: one function,
documented gotchas in a top comment, a two-layer `IMPORTED`/`ALIAS` target result.
