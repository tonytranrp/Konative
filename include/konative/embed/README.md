# include/konative/embed/

The runtime half of Konative's self-checking embedded-blob mechanism. The build-time half is
`cmake/modules/KonativeEmbedBlob.cmake`'s `konative_embed_binary_blob()` — read that file's doc
comment first; this folder only makes sense paired with it.

## Hard rules

- **This is a build-integrity self-check, not a tamper/security boundary.** The expected SHA-256
  is linked in cleartext right next to the data it checks — anyone who can modify the embedded
  blob can trivially recompute and patch the expected hash too. `verify_blob()` exists to turn "the
  build pipeline embedded the wrong/truncated/stale bytes" into a clear, actionable startup error
  instead of a mysterious crash deep inside a classloader — don't extend this module to try to
  detect deliberate tampering, that's a different, unrelated problem this module doesn't solve.
- **Operates on `std::span<const unsigned char>`, not `std::byte`.** The embedded blob symbols
  `konative_embed_binary_blob()` generates are declared `extern const unsigned char
  <prefix>_start[]` (matching the existing GameHub-derived convention) — matching that type here
  avoids a pointless cast at every call site. `std::byte` doesn't implicitly convert to
  `unsigned char` (deliberately, by design) so PicoSHA2's own iterator-based `hash256()` can't
  consume a `std::byte` range directly either way.
- **`Result<T, E>` (`core/result.hpp`), never an exception, for the verification outcome** — this
  runs at `JNI_OnLoad` time, at the JNI boundary, where an unwound C++ exception cannot safely
  cross (`ARCHITECTURE.md` section 6.3's interop-boundary rule applies here too, not just to
  Kotlin/Native).

## Adding to this folder

A new file here should be a new build-integrity check paired with a corresponding
`KonativeEmbedBlob.cmake`-side generator, following `checked_blob.hpp`'s shape (one pure function,
`Result<T, E>` return, no hidden global state). Don't add anything that needs a live JNI/Android
context — that belongs in the platform/android JNI loader that calls into this module, not here.
