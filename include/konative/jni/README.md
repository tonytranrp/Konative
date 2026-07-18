# include/konative/jni/

JNI reference-counting and call helpers, plus the embedded-dex class loader — the mechanics behind
`ARCHITECTURE.md` section 6.4/6.6's `JNI_OnLoad`-to-loaded-Compose-class bridge. Ported from
`GameHub`'s real, working `libs/jni/` (see each file's own top comment for the exact source), not
reinvented — `ref.hpp`'s `LocalRef`/`GlobalRef` and `call.hpp`'s call helpers are GameHub's own
design, adapted to be fully inline and to use this project's own `Result<T, E>`/logging
conventions instead of GameHub's.

## Hard rules

- **`Result<T, E>` (`core/result.hpp`), never a falsy-struct-on-failure return, for anything
  fallible here.** `dex_loader.hpp`'s `DexLoadError` enum names which specific step failed
  (`ActivityThreadClassNotFound`, `CurrentApplicationNull`, ...) — this is a deliberate departure
  from `GameHub`'s own `LoadedDexClass{}` (empty-struct-means-failure) shape, not an oversight;
  match this convention for any new fallible function added here, don't reintroduce the
  falsy-struct pattern.
- **`LocalRef`/`GlobalRef` are the ONLY way a JNI reference is held past the JNI call that produced
  it.** Never store a raw `jobject`/`jclass`/etc. — a leaked local ref exhausts the local ref table
  (a real, silent-until-it-crashes failure mode); a `GlobalRef` whose `reset(env)` never gets
  called leaks for the life of the process.
- **`check_and_clear_exception()` after every JNI call that can throw, before any further JNI
  call.** A pending exception left unchecked poisons every subsequent JNI call on that `JNIEnv*`,
  including the `DeleteLocalRef`/`DeleteGlobalRef` calls `LocalRef`/`GlobalRef`'s own destructors
  make — this is why `check_and_clear_exception` is a free function here, not folded silently into
  `call_static_method`/`call_instance_method` only.
- **The one hidden-API call in this whole module is `ActivityThread.currentApplication()`, and it
  stays exactly there.** Verified against Google's own published `hiddenapi-flags.csv`
  (`research/jni_activity_bootstrap_research.md` section 1.2) to be safe for this project's API
  range — don't add a second reflective/hidden-API call elsewhere in this module without the same
  level of verification first.
- **This module is Android-only** (`<jni.h>` and everything built on it) — gated `if(ANDROID)` in
  the parent `CMakeLists.txt`, same as `platform/android`. Don't add anything here that's meant to
  build on `desktop-debug`.

## Adding to this folder

A new file here should be a new JNI mechanics helper (a new reference type, a new call-shape
helper) or a new embedded-dex-loading step — not application logic. The Compose-specific handoff
(`Application.ActivityLifecycleCallbacks`, `ComposeView` wiring) belongs in the embedded dex's own
Kotlin code, not here — see `ARCHITECTURE.md` section 6.6's "favor real Kotlin over reflection"
reasoning.
