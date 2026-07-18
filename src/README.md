# src/

The only `.cpp` translation units in the whole framework (`ARCHITECTURE.md` §2) — genuinely
load-bearing entry points and glue that can't be header-only, not a default home for "regular"
code.

## Hard rules

- **Everything that *can* be header-only, is** — this folder exists only for real entry points
  (`platform/android/jni_onload.cpp`, `ARCHITECTURE.md` section 6.4 — the `JNI_OnLoad(JavaVM*,
  void*)` callback `System.loadLibrary()` invokes automatically, which needs a real translation
  unit for its `extern "C" JNIEXPORT` linkage) and any future explicit-template-instantiation choke
  points. Before adding a `.cpp` here, ask whether the same code could be a `.hpp` in
  `include/konative/` instead — the default answer should be yes.
- **No business/gameplay logic.** A `.cpp` added here should be almost entirely calls into
  `include/konative/**.hpp` — if a file here is doing real work rather than wiring, that work
  probably belongs in a header instead, with only the unavoidable platform entry point left as
  `.cpp`.
- **`platform/android/` has no C++ header counterpart under `include/konative/`.** An earlier
  design (`include/konative/platform/android/` declaring a `run_application`/`pump_once` contract
  for `android_native_app_glue`) was deleted along with that whole event-loop model (commit
  `3618fb5`) — `jni_onload.cpp` is a self-contained entry point calling straight into
  `konative::jni`/`konative::embed`, not a contract implementation mirroring a sibling header
  folder. Don't recreate a `include/konative/platform/android/` folder on the assumption this rule
  still describes the current design.

## Adding to this folder

Only add a `.cpp` file here for a genuinely new load-bearing entry point (a new platform's native
entry, a new explicit-instantiation choke point) — never as a shortcut to avoid header-only
discipline elsewhere.
