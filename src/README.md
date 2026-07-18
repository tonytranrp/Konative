# src/

The only `.cpp` translation units in the whole framework (`ARCHITECTURE.md` §2) — genuinely
load-bearing entry points and glue that can't be header-only, not a default home for "regular"
code.

## Hard rules

- **Everything that *can* be header-only, is** — this folder exists only for real entry points
  (`platform/android/android_main.cpp`), the actual `android_native_app_glue` event-loop
  implementation (`activity_bridge.cpp`, `looper_pump.cpp` — these need a real translation unit
  because they define the C callback `android_native_app_glue` invokes and drive a blocking poll
  loop, neither of which belongs in a header), and any future explicit-template-instantiation
  choke points. Before adding a `.cpp` here, ask whether the same code could be a `.hpp` in
  `include/konative/` instead — the default answer should be yes.
- **No business/gameplay logic.** A `.cpp` added here should be almost entirely calls into
  `include/konative/**.hpp` — if a file here is doing real work rather than wiring, that work
  probably belongs in a header instead, with only the unavoidable platform entry point left as
  `.cpp`.
- **Mirrors `include/konative/platform/android/`'s folder shape 1:1** — `src/platform/android/`
  exists specifically because that module's headers declare a contract
  (`run_application`/`pump_once`) that needs exactly one real implementation.

## Adding to this folder

Only add a `.cpp` file here for a genuinely new load-bearing entry point (a new platform's native
entry, a new explicit-instantiation choke point) — never as a shortcut to avoid header-only
discipline elsewhere.
