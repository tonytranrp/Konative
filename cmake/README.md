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
  moving branch ref even with a populated source cache. **Verify a new/changed tag actually exists**
  (`git ls-remote --tags <repo-url>`) before committing it — this repo's own first real build
  caught a fabricated `glaze` tag that never existed; see `BUILDING.md`.
- **`CMAKE_POLICY_VERSION_MINIMUM` must never be set globally in `CMakePresets.json`** — CMake's
  own docs say project code should not set this as a way to fix its own policy version, since it
  silently lowers the floor for every CPM dependency, not just the one that needs it. It was set
  globally here for one revision and a code review caught it; the actual, narrow fix lives in
  `modules/KonativeDependencies.cmake` as a `set(CMAKE_POLICY_VERSION_MINIMUM ...)` /
  `unset(...)` pair wrapped tightly around just the one `CPMAddPackage()` call that needs it
  (currently `doctest`, whose `cmake_minimum_required` floor is below what CMake 4.x still
  supports running at all — a hard configure error, not just a deprecation warning). If a new
  dependency needs the same treatment, give it its own scoped `set()`/`unset()` pair around just
  that `CPMAddPackage()` call — never widen the scope back to every preset.
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
