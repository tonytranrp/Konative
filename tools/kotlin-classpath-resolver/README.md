# kotlin-classpath-resolver/

Closes a gap `cmake/modules/KonativeEmbedKotlinDex.cmake`'s own top comment used to document as open:
*"real Maven dependency resolution from CMake is still an open problem."* CMake has no Maven-aware
dependency resolver of its own, and `embedded_kotlin/`'s Compose UI needs a real, correctly-resolved
AndroidX/Compose/coroutines closure to compile against. Before this project existed, that closure was
assembled by hand, once per machine, into two flat directories
(`KONATIVE_KOTLIN_CLASSPATH_DIR`/`KONATIVE_AAPT2_AAR_DIR`) that
`cmake/modules/KonativeCompileKotlinDex.cmake` consumes directly. This project automates that
assembly with real Gradle dependency resolution instead.

**Not an app module.** It never produces an APK/AAR of its own — `com.android.library` is used only
because AGP's own artifact-transform registration (AAR → classes.jar) is what makes AAR dependency
resolution produce usable jars at all; this machine's real, working `testapp/` build already proves
this exact AGP+SDK setup works, so this reuses that proven mechanism rather than a novel, untested
plain-Gradle path. `testapp/`'s own Gradle build never depends on or invokes this project — they are
both standalone Gradle roots under this repo, wired together by nothing but convention (this one's
output directories match the CMake variables the other pipeline expects).

## Running it

```
cd tools/kotlin-classpath-resolver
./gradlew resolveKonativeClasspath      # gradlew.bat on Windows cmd/PowerShell directly
```

Requires `local.properties` with `sdk.dir=<path to your Android SDK>` (gitignored — machine-local,
same convention as `testapp/local.properties`). The wrapper is pinned to Gradle 9.4.1 (the exact
version this project was built and verified against) so no system-wide Gradle install is needed.

Output lands in `resolved-output/` (gitignored, regenerated on every run):
- `resolved-output/kotlin-classpath/*.jar` — point `KONATIVE_KOTLIN_CLASSPATH_DIR` at this directory
- `resolved-output/aapt2-aars/*.aar` — point `KONATIVE_AAPT2_AAR_DIR` at this directory

## Real bugs found building this (2026-07-22)

1. **AGP's `"android-classes-jar"` artifact transform strips `META-INF/*.kotlin_module`** — the
   Kotlin multi-file-facade metadata a direct `kotlinc` invocation needs to resolve a top-level
   extension function/property spread across multiple source files in one module (e.g.
   `Modifier.background()`, `Modifier.focusable()` — virtually every Compose extension function is
   declared this way). AGP's transform correctly omits it because AGP's own internal javac/d8
   pipeline never needs it, but `kotlinc` does. Symptom: dozens of misleading "unresolved reference"
   errors on ordinary imports, with the actual target `.class` file genuinely present and
   byte-identical to a known-working jar. **Fix**: `exportClasspathJars` extracts `classes.jar`
   directly from each AAR's own raw zip contents (`"aar"` artifact type + `zipTree()`), never through
   AGP's transform.
2. **The same underlying dependency can resolve as two differently-named `File` entries with
   byte-identical `classes.jar` content** (`androidx.print:print:1.0.0` was the real case — one
   real `print-1.0.0.aar`, one genercially-named `classes.aar`; root cause not fully chased down,
   plausibly an AGP-internal repackaging transform also matching the `"aar"` attribute). r8 refuses
   to dex two input jars that both define the same class. **Fix**: SHA-256 content-hash dedup
   (`seenHashes`), not name/path dedup.
3. **A resource-only AAR with no `classes.jar` entry at all** (`androidx.print:print:1.0.0` again)
   makes `zipTree(aar).matching{include("classes.jar")}.files.singleOrNull()` return a **phantom
   file** — a valid but genuinely empty zip (bare 22-byte end-of-central-directory record) — instead
   of `null`. **Fix**: check the real zip entry count (`ZipFile(...).entries().hasMoreElements()`),
   plus an unconditional final cleanup pass over the whole output directory for anything that still
   slips through as a genuinely empty jar.

## Status

Verified equivalent to the hand-assembled classpath it replaces: a real
`cmake --build`(kotlinc+aapt2+r8) succeeded end-to-end using this project's own output, and a real
on-device install+launch on the connected LDPlayer emulator showed clean logcat, all self-checks
passing, and correct touch-input handling.

**In CI (`.github/workflows/android-build.yml`), this resolver's output is the ONLY classpath
source** — there is no hand-assembled fallback available on a hosted runner at all; the workflow
runs `./gradlew resolveKonativeClasspath` and passes `resolved-output/kotlin-classpath`/
`resolved-output/aapt2-aars` straight into the CMake configure step. **Locally, this dev machine's
own `CMakeUserPresets.json` still points at the original hand-assembled directories, by choice, not
necessity** — left alone since switching it isn't needed for anything to keep working, not because
this resolver can't also serve local dev. Dependency versions are pinned to the exact set already
proven working
(`embedded_kotlin/README.md`'s own hard-won bug-fix history) rather than the newest available
(`androidx.activity:activity:1.13.0`, `kotlinx-coroutines-android:1.11.0` both exist) — upgrading is a
deliberate future decision, not a side effect of this project existing.

A Gradle deprecation warning ("incompatible with Gradle 10") appears on every run — pre-existing,
from AGP 8.5.2 against Gradle 9.4.1, not introduced by anything in this project's own build script.
Not chased further since the exact pairing is already proven working end-to-end above.
