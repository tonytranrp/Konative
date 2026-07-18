# include/konative/interop/

The Kotlin/Native ⇄ C++ C-ABI boundary — the single highest-uncertainty module in the whole
framework (`ARCHITECTURE.md` §6.3/§9). Read that before touching anything here.

## Hard rules

- **Every function crossing this boundary in either direction is a flat, `@CName`-exported C
  symbol — never the generated `_api.h` nested-struct surface.** `@CName("konative_...")` on the
  Kotlin side, a matching `extern "C"` forward declaration in `kotlin_native_bridge.hpp` on the
  C++ side. Never introduce `KotlinNative_ExportedSymbols`-style struct indirection — it exists in
  Kotlin/Native's own tooling but this project deliberately doesn't use it (`ARCHITECTURE.md` §6.3
  explains why: the flat `@CName` symbols are far more ergonomic for a hand-authored boundary).
- **No C++ exception may ever unwind across a function declared in this module.** An unwound
  exception cannot safely cross a plain-C-ABI call into Kotlin/Native code — use
  `konative::core::Result<T,E>` (`core/result.hpp`) for anything that can fail across this
  boundary, and catch-and-convert at the very last C++ frame before the `extern "C"` call.
- **Explicit symbol visibility on both sides of every boundary function**, via
  `detail/symbol_visibility.hpp`'s `KONATIVE_ABI_EXPORT` — never rely on compiler default
  visibility here. This is a direct, deliberate mitigation for the documented libc++/symbol
  conflicts this exact Kotlin/Native+NDK combination has hit elsewhere (`ARCHITECTURE.md` §6.3).
- **Keep this module's surface small, on purpose.** Every new cross-boundary call is a new spot
  where the unresolved linking risk (`ARCHITECTURE.md` §9) can surface — don't add a new
  `@CName` function here speculatively; add it only when a real caller on the other side needs it,
  and prove the round-trip actually links and runs before building more architecture on top of it.
- **This module owns the boundary contract, never the implementation on either side.** No EGL/
  GLES/Vulkan calls belong here (that's `native/src/`'s job, reached *through* this boundary) and
  no ECS/gameplay logic belongs here either (that's `ecs/`'s job, calling *into* this boundary).

## Adding to this folder

A new file here should be a new named boundary contract (a new set of related `@CName`
declarations + their `extern "C"` C++ counterparts), matching one real Kotlin/Native source file's
exports. Verify the link before committing further code that depends on it.
