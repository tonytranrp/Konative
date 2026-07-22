# Building Konative

## Quick start (desktop, no Android/Kotlin toolchain needed)

```sh
cmake --preset desktop-debug
cmake --build build/desktop-debug
./build/desktop-debug/tests/konative_tests.exe
```

**If step 1 fails with `fatal: ambiguous argument 'HEAD0'`**, you have the same npm-`git.cmd`-shim
issue this repo's own dev machine has — add `-DGIT_EXECUTABLE="<path to the real git.exe>"` (see
the dedicated troubleshooting entry below for how to find it) to the `cmake --preset` command and
retry. This is common enough on Windows dev machines with Node.js installed that it's called out
here up front, not just in troubleshooting.

This is the preset actually exercised so far — verified end to end (configure, build, and the
resulting test/example binaries actually run) on 2026-07-17. `android-arm64`/`android-x86_64`
need a real Android NDK; see `CMakeUserPresets.json` (git-ignored, machine-local — create your own
pointing at your real NDK/SDK paths; do not commit one).

## Continuous integration

`.github/workflows/desktop-tests.yml` builds and runs the full desktop test suite on every push/PR
to `main` — a real, hosted-runner check for exactly what this doc's own Quick Start does. It's
**desktop-only**: the Android presets need a real NDK plus the machine-local toolchain paths
`CMakeUserPresets.json` provides, which a hosted runner doesn't have without real, separate setup
work not yet done here. When `ARCHITECTURE.md` says something is "verified in CI," it means this
workflow, for whatever subset of the desktop test suite actually covers that claim — not every
claim in this project has Android-side CI coverage yet.

## Troubleshooting — real, reproduced issues and their fixes

These were all hit and fixed getting the very first real build working on a real Windows machine
— recorded here so nobody re-derives them from scratch.

### `CMake Error ... Invalid character escape '\x'` in a `CMakeLists.txt` string

Long since fixed in this repo (was a stray literal `\xc2\xa7` — an escaped UTF-8 § symbol that
never got interpreted as the actual character — inside `option()`/`message()` string literals).
If you see this again after editing a `CMakeLists.txt`, you introduced the same class of bug:
don't put raw backslash-escape sequences inside CMake string literals; CMake's own string parser
interprets `\x` as an escape attempt and fails on anything that isn't valid hex.

### `fatal: ambiguous argument 'HEAD0'` / `fatal: not a git repository: '.git'` during a CPM/FetchContent git step

**Cause**: if `git` resolves (via `find_program`/`find_package(Git)`) to a **`.cmd` batch-file
wrapper** rather than the real `git.exe` — e.g. an npm-installed `git.cmd` shim ahead of Git for
Windows in `PATH` — Windows batch files treat `^` as their own escape character. CMake's generated
git-update scripts run `git rev-parse "HEAD^0"`; through a `.cmd` wrapper the `^` gets silently
consumed, so git actually receives the literal argument `HEAD0` and fails.

**Fix**: force the real `git.exe` explicitly on the configure command line:
```sh
cmake --preset desktop-debug -DGIT_EXECUTABLE="C:/Program Files/Git/cmd/git.exe"
```
(adjust the path to wherever Git for Windows is actually installed — `where git` lists every
match on `PATH` in order; pick a `.exe`, never a `.cmd`).

### `CMake Error ... Compatibility with CMake < 3.5 has been removed from CMake`

Some older CPM-fetched dependencies (hit with `doctest` specifically) declare a
`cmake_minimum_required` version below what CMake 4.x still supports running at all — not just a
deprecation warning, a hard configure error. Already fixed, but **not** at the preset level —
CMake's own docs say project code should not set `CMAKE_POLICY_VERSION_MINIMUM` globally that way,
since it silently lowers the floor for every CPM dependency, not just the one that needs it. The
real fix is scoped narrowly in `cmake/modules/KonativeDependencies.cmake`: a
`set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` / `unset(...)` pair wrapped tightly around just the
`doctest` `CPMAddPackage()` call. If a *newly* added dependency hits this with an even-lower floor,
give it the same narrow treatment — its own `set()`/`unset()` pair around just that call — don't
widen the scope back to every preset, and don't patch the vendored dependency's own
`CMakeLists.txt`.

### `consteval function ... is not a constant expression` deep inside `fmt`/`spdlog` headers

A real version-compatibility gap between `fmt 10.2.1` (originally pinned) and a very new Clang
(22.1.4 on this machine, from a recent llvm-mingw build) — `fmt`'s compile-time format-string
checking got rejected under this compiler's stricter `consteval` handling. Fixed by bumping to
current releases (`fmt 12.2.0` + `spdlog v1.17.0` — see `cmake/modules/KonativeDependencies.cmake`
for the exact pins and its own comment on this). If you deliberately pin an older `fmt`/`spdlog`
for some other reason, expect to hit this again on a bleeding-edge compiler.

### `undefined symbol: doctest::detail::ResultBuilder::...` (a wall of doctest linker errors)

Not a dependency bug — a bug in **this repo's own** `tests/` setup, now fixed
(`tests/main.cpp`). doctest is header-only but needs exactly one translation unit to define
`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` before `#include <doctest/doctest.h>`, which is what actually
instantiates its runtime implementation and provides `main()`. Every other `test_*.cpp` just
`#include`s doctest plain — see `tests/README.md`.

### A dependency's own `CMakeLists.txt` fails with a stray `/` in a source-file list, or a submodule git-update failure

Hit with `libcoro` specifically: v0.14.1's own `CMakeLists.txt` had a genuine upstream typo
(`include/coro/detail/task_self_deleting.hpp / src/detail/task_self_deleting.cpp` — a stray `/`
between two file paths, parsed as its own list entry, so `add_library` fails looking for a source
file literally named `/`), and separately its `vendor/c-ares/c-ares` submodule (needed only for
`LIBCORO_FEATURE_NETWORKING`, which this repo has off) hit a git-update failure on this machine.
Fixed by bumping to `v0.16.0` (typo fixed upstream). The submodule git-update failure itself was
actually fixed by the `-DGIT_EXECUTABLE` workaround above (it's a general git-invocation fix, not
specific to this dependency) — **not** by also passing `GIT_SUBMODULES ""`, despite that flag being
present on this exact `CPMAddPackage()` call: CMake's CMP0097 policy defaults to `OLD` here (never
set to `NEW` anywhere in this repo), under which an empty `GIT_SUBMODULES` string means "unset," not
"fetch none" — the ~559-file `c-ares` submodule is still checked out in full regardless of that
flag. It's harmless (unused, just wasted clone time/disk), left in place as a statement of intent
rather than removed — see `cmake/modules/KonativeDependencies.cmake`'s own comment on this exact
line for the full correction. If you hit an upstream bug like this in some other CPM dependency:
check for a newer tag first; only reach for CPM's `PATCHES` option if no fixed release exists yet.

### A CPM `GIT_TAG` doesn't exist at all

Verify any new/changed pin with `git ls-remote --tags <repo-url>` before trusting it — one was
found and fixed in this repo already (`glaze` was pinned to a `v3.5.4` that never existed; the
real tag history jumps from an early 3.x/2.x line straight to a current 7.x line).
