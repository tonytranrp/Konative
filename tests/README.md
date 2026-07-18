# tests/

Unit tests for `include/konative/**.hpp`, using doctest (chosen over Catch2 specifically for its
near-zero compile-time overhead — see `ARCHITECTURE.md` §4, a real budget concern for a
template-heavy header-only codebase).

## Hard rules

- **`main.cpp` is the ONLY file that may define `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.** doctest is
  header-only but needs exactly one translation unit to instantiate its actual runtime
  implementation (`Result`/`ExpressionDecomposer`/etc.) and provide `main()` — every other
  `test_*.cpp` just plain `#include <doctest/doctest.h>` and defines `TEST_CASE`s. Forgetting this
  produces a wall of `undefined symbol: doctest::...` linker errors with no compile-time warning
  at all — verified directly (this is exactly the bug this project's own first real build attempt
  caught).
- **One `test_<module>.cpp` per module** (`test_result.cpp` for `core/result.hpp`,
  `test_events.cpp` for `events/`), mirroring `include/konative/`'s own folder-per-concern shape —
  don't batch unrelated modules' tests into one file, and don't split one module's tests across
  many files without a real reason.
- **Tests must build and run on desktop (`desktop-debug` preset).** Anything genuinely Android-only
  (real EGL/GLES/Vulkan behavior, real lifecycle callbacks) is NOT unit-testable here — it belongs
  in `testapp/`'s on-device verification loop (`ARCHITECTURE.md` §13) instead. Don't try to mock
  Android platform APIs just to get a test running here; that's the wrong layer for it.
- **A test for a `detail/` header tests it through its module's public API where possible**, not
  by reaching into `detail::` namespaces directly — if a `detail/` helper genuinely can't be
  exercised through the public surface, that's a signal the public API is missing something, not
  a reason to test the internal directly as a matter of course.

## Adding a new test file

Add `test_<module>.cpp` to `tests/CMakeLists.txt`'s `add_executable()` source list. `doctest`'s
CMake integration (`doctest_discover_tests`) picks up new `TEST_CASE`s automatically — no other
registration needed.
