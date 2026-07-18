# Konative — Research Report

Compiled from: 2 web-research subagents (Android dex-embedding/packer prior art; corrosion-style
CMake tooling architecture), 1 local-codebase subagent (deep dive on `GameHub`'s loader/dynload/
JNI/ABI internals), and direct inspection of `GameHub` by the orchestrating session. All claims
below are sourced — either to a URL (web research) or to a `GameHub` file path (local research).

---

## 0. The fork this report surfaces first

Konative's own `README.md` already commits to one specific architecture:

> "Konative compiles Kotlin straight to a single native `.so` via **Kotlin/Native**, and wires it
> directly into Android's own NativeActivity/GameActivity ... no JVM, no dex, no hand-written JNI."

That is the **Kotlin/Native (AOT-to-machine-code) path** — Kotlin source is compiled by
`kotlinc-native` straight to ARM64/x86_64 machine code and linked into the `.so` like any other
object code. There is no JVM at runtime, no `classes.dex`, no `InMemoryDexClassLoader`, no JNI
reflection bootstrap.

The chat request that spawned this research described a **different** mechanism as the intended
core trick — **dex embedding**: compiling Kotlin/Java to JVM bytecode, dexing it, embedding the
`.dex` bytes as linked data inside the `.so`, and having native code construct an
`InMemoryDexClassLoader` from that blob at runtime to run it *inside the host app's already-live
ART VM*. This is a JVM-bytecode-interpretation path, not an AOT-native-code path.

**These are two genuinely different, non-interchangeable architectures**, and `GameHub` — the
reference repo this research leans on — actually ships **both**, as two independent CMake modules
solving two different problems (`cmake/modules/KotlinNative.cmake` for the AOT path,
`cmake/modules/JvmDex.cmake` for the dex-embedding path). Section 7 below lays out the concrete
trade-offs. **This is a real decision Konative needs to make (or explicitly support both, the way
GameHub does) before the CMake API design is finalized** — the rest of this document gives the
technical grounding for making that call, but does not make it.

---

## 1. Dex embedding: how it actually works, and who already does it

### 1.1 The runtime mechanism (verified against real, working code in `GameHub`)

Two Android APIs make this possible:

- **`dalvik.system.InMemoryDexClassLoader`** (API 26+) — constructed from a **`ByteBuffer`**
  wrapping already-in-memory dex bytes. Per the [Android API reference](https://developer.android.com/reference/dalvik/system/InMemoryDexClassLoader),
  the dex data "is never written to the local file system and only resides in the buffer." This is
  the load-bearing API — its API-26 floor is *the* reason any dex-embedding design needs
  `--min-api 26` and is why `GameHub`'s `JvmDex.cmake` hardcodes that default (see 1.2).
- **`android.app.ActivityThread.currentApplication()`** — an unofficial (not public SDK) static
  method the Android framework itself relies on internally to get the process's one `Application`
  instance from anywhere. It's the one reflective/hidden-API call the whole mechanism needs; every
  other step is ordinary public JNI/SDK usage.

End-to-end, as implemented in `GameHub/libs/jni/src/dex_loader.cpp` and described in
`GameHub/Module/Login/README.md` ("Kotlin/DEX embedding" section) and
`GameHub/Module/Menu/README.md`:

1. Native code (already running because the `.so` was `dlopen`'d/`System.loadLibrary()`'d into an
   already-live ART process — no separate JVM bootstrap needed) reflectively calls
   `ActivityThread.currentApplication()` to get an `Application` object from just a `JavaVM*`.
2. Calls `application.getClassLoader()` — this becomes the **parent** of the new classloader, so
   the embedded dex's references to `android.*` framework classes resolve normally.
3. Wraps the embedded dex byte array in a direct `ByteBuffer` (`env->NewDirectByteBuffer`, pointing
   directly at the linked, read-only data already inside the caller's own `.so` — no copy, no temp
   file).
4. Constructs `new InMemoryDexClassLoader(dexBuffer, parentLoader)`.
5. Calls `dexClassLoader.loadClass("com.example.SomeClass")` via **reflection**, not
   `env->FindClass()` — `FindClass` resolves relative to the classloader associated with the
   *calling* native method's own class, which doesn't apply here since this call didn't originate
   from Java. This is the standard, documented workaround.
6. The resulting `jclass` is promoted to a `GlobalRef` and used from then on via ordinary
   `CallStaticVoidMethod`/etc.

Full working reference implementation: [`GameHub/libs/jni/src/dex_loader.cpp`](../../../GameHub/libs/jni/src/dex_loader.cpp)
(98 lines) and its header, [`GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp`](../../../GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp).

### 1.2 The build-time mechanism (verified, working CMake in `GameHub`)

`GameHub/cmake/modules/JvmDex.cmake` exposes one function, `gamehub_jvm_dex_blob(name JAVA_SRC_DIR <dir> [MIN_API <n>])`:

```
Kotlin (.kt) or Java (.java) source
  → kotlinc -include-runtime -jvm-target 1.8 -no-reflect -cp android.jar   → name_classes.jar
    (or: javac -bootclasspath android.jar -source 8 -target 8 + jar cf    → name_classes.jar)
  → d8 --output <dir> --min-api 26 name_classes.jar                       → classes.dex
  → cmake/scripts/bin_to_c_array.py classes.dex name_dex.cpp name_dex.h   → linked C++ byte array
  → add_library(name_jvm_dex STATIC name_dex.cpp)
  → add_library(gamehub::jvmdex::name ALIAS name_jvm_dex)
```

Empirically-verified facts baked into the CMake (real, not assumed — see the file's own top
comment for the exact tool versions this was checked against: javac 17 / kotlinc-jvm 2.4.0 / d8
8.10.9):

- `d8`'s `--output` directory must **already exist** — it errors instead of creating it.
- `d8` rejects a bare directory of `.class` files ("Unsupported source file type") — wants
  individual `.class` paths or a jar/zip. This is why both the `javac` and `kotlinc` paths produce
  an intermediate jar before dexing.
- `-include-runtime` bundles the **entire** `kotlin-stdlib.jar` into the dex, with **no tree-shaking**
  at the `kotlinc` CLI level (that's normally R8/ProGuard's job in a full Gradle build, deliberately
  not used here). Confirmed via a real round-trip: a near-trivial one-function Kotlin `object`
  dexes to **~2.5 MB** vs a few KB for the equivalent plain-Java version. This is real, unavoidable
  size cost with this toolchain — not a mistake — unless a shrinking pass is added.
- `@JvmStatic` is **not cosmetic**. A Kotlin `object`'s member function is a genuine instance
  method by default (callable only via a hidden static `INSTANCE` field); `env->GetStaticMethodID()`
  for it returns `null` with **zero warning at compile or dex time**. Every JNI-callable entry
  point must be `@JvmStatic`.
- Full source: [`GameHub/cmake/modules/JvmDex.cmake`](../../../GameHub/cmake/modules/JvmDex.cmake)
  (237 lines); the byte-array generator is
  [`GameHub/cmake/scripts/bin_to_c_array.py`](../../../GameHub/cmake/scripts/bin_to_c_array.py) (48 lines,
  trivial — reads the binary, emits `extern const unsigned char k<Name>Bytes[]` / `k<Name>Size` in a
  header + matching `.cpp`).

### 1.3 The app shell this produces, proven in `GameHub/testapp`

The entire Java side of `GameHub`'s test app is:

```java
public class MainActivity extends Activity {
    static {
        System.loadLibrary("gamehub_loader");
    }
}
```

(`GameHub/testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java`, 17 lines total,
comment: *"The ONLY Java code in this project ... Its entire job is loading the native Loader
`.so` — every other concern ... lives in gamehub_loader's native code."*)

This is a **working, on-device-proven instance** of exactly the "app is just `loadLibrary()`"
shape Konative wants — with the caveat that `GameHub`'s Kotlin UI overlays (Menu, Login, Render)
are still separately-embedded-per-module dex blobs loaded on demand, not one single monolithic
blob baked in at APK build time. Whether Konative wants per-module or one-big-blob is an open
design choice, not something this prior art forces either way.

### 1.4 Who else does this (dex-in-native-binary is well-trodden, just not as an open framework)

This exact "embed dex bytes in a native library, JNI-construct a classloader from a `ByteBuffer`"
pattern is the core trick of essentially every major Android app-hardening/"reinforcement"
vendor, going back over a decade — Konative would be the first to do it as an **open, documented,
legitimate developer framework** rather than a packer's undocumented internal:

- **Bangcle** — ships an innocuous stub `classes.dex`; native `libsecexe.so` decrypts the real
  `classes.jar` **in memory** at runtime via a custom `MyClassLoader`.
- **Ijiami** — stub `NativeApplication`/`SuperApplication` classes; real logic loaded via
  `libexec.so`/`libexecmain.so`.
- **Qihoo 360 (Jiagu)** — its `libjiagu*.so` **parses the protected dex itself in native code**
  instead of using ART's normal dex parsing, and deliberately zeroes `class_data_off` in the dex
  header after load so a memory dump taken post-load yields a structurally-broken dex.
- Commercial SDKs formalizing this as a product feature: **DexProtector** (Licel — explicitly
  supports protecting native libraries containing JNI functions), **Appdome** (a "Dex Relocation"
  plugin doing call-obfuscation/function-call modification), **DexGuard** (Guardsquare).
- Academic literature documenting the mechanism in the most implementation-level detail (better
  primary sources than any vendor marketing page): **DexHunter**
  ("[Toward Extracting Hidden Code from Packed Android Applications](https://www4.comp.polyu.edu.hk/~csxluo/DexHunter.pdf)"),
  **AppSpear** ("[Bytecode Decrypting and DEX Reassembling for Packed Android Malware](https://lijuanru.com/publications/jss18.pdf)"),
  the ACM Computing Surveys paper
  ["Practical Android Software Protection in the Wild"](https://dl.acm.org/doi/10.1145/3757735)
  (taxonomizes 28 real tools across ~2.5M apps), and a 2026 arXiv survey
  ["To Unpack or Not to Unpack"](https://arxiv.org/html/2509.16340v1) (notes production packers
  routinely stream/decrypt only the bytecode needed at a given moment rather than materializing a
  full valid dex at once — Chinese vendors alone account for ~59% of Chinese-market app-protection
  adoption).
- Open, non-commercial reference implementations: `huaxiaozhou/android-protection`'s
  [`load.c`](https://github.com/huaxiaozhou/android-protection/blob/master/LoadDex/DexLoaderJni/jni/load.c)
  (AES-decrypts an asset, writes a jar to cache, swaps `LoadedApk.mClassLoader` via reflection —
  still file-based, not pure in-memory) and `Gyoonus/android_dynamic_loader`
  ([GitHub](https://github.com/Gyoonus/android_dynamic_loader), explicitly described as usable "to
  make Android App Packer," pure `InMemoryDexClassLoader`-based).

**Practical implication for Konative**: the mechanism is not novel and is extremely well
understood defensively (i.e., security researchers already know exactly how to detect/unpack it).
Konative's differentiation is doing it **openly and ergonomically as a build tool**, not the
technique itself.

### 1.5 A harder alternative worth knowing about: running Java/dex with no Activity/APK at all

If Konative ever wants to *not* depend on the host app's already-running ART instance (e.g. a
pure-native process that isn't launched as a normal Activity), two real approaches exist:

- Dynamically resolving `JNI_CreateJavaVM` (not exported by the NDK on purpose) out of
  `libnativehelper.so`/`libdvm.so`/ART's runtime lib via `dlsym`, as documented in Caleb Fenton's
  ["Creating a Java VM from Android Native Code"](https://calebfenton.github.io/2017/04/05/creating_java_vm_from_android_native_code/) —
  fragile (private symbols), but works.
- Reusing Android's own `app_process` binary as a launcher (`app_process /path/to/jar
  com.example.ClassName` with `CLASSPATH`/`LD_LIBRARY_PATH`/`ANDROID_DATA` env vars set), per
  Yrom's ["How to run Java standalone app (with JNI) on Android without creating an apk"](https://yrom.net/blog/2023/07/07/run-java-with-jni-app-on-android/) — needs shell/root-level
  filesystem access, bypasses the normal permission model.

Neither is needed for Konative's stated goal (an app hosted normally, just with a trivial Java
shell) — reusing the existing `JNI_OnLoad(JavaVM*, void*)` entry point `System.loadLibrary()`
already gives you, exactly as `GameHub` does, is the right default and avoids all of this fragility.

---

## 2. Native `.so` loading: what a "custom loader" should and shouldn't be

This section matters because the chat prompt referenced "my code ... about how dex embedding
works" and "the libs itself is a custom loader I made" — `GameHub`'s `libs/dynload` was assumed
going in to be a hand-rolled ELF loader. **It is not, and that turns out to be an important, deliberate
design decision worth inheriting rather than re-deriving.**

### 2.1 What `GameHub/libs/dynload` actually is

A thin RAII wrapper (`library_handle.hpp/.cpp`, `symbol_resolver.hpp/.cpp`, `abi_verifier.hpp/.cpp`)
around **the real OS dynamic linker's own APIs** — nothing about ELF parsing, relocation, symbol
resolution, TLS setup, or `.init_array` execution is reimplemented. Two load paths:

- **Disk path**: ordinary `::dlopen(path, RTLD_NOW|RTLD_LOCAL)`.
- **In-memory path** (`LibraryHandle::open_from_memory()`,
  [`GameHub/libs/dynload/src/library_handle.cpp`](../../../GameHub/libs/dynload/src/library_handle.cpp) lines ~98–163):
  1. Resolve `memfd_create` via `dlsym(RTLD_DEFAULT, "memfd_create")` **at runtime** (not linked —
     confirmed the symbol isn't linkable against this project's API-24 stub libs even though a real
     API-30+ device's libc has it; `nullptr` back means "unavailable, API < 30").
  2. `fd = memfd_create(debug_name, MFD_CLOEXEC)` — an anonymous fd with **no directory entry
     anywhere**.
  3. `write(fd, bytes.data(), bytes.size())`.
  4. `fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE)` — defense-in-depth, converts "promise not to write
     again" into a kernel-enforced guarantee.
  5. `android_dlopen_ext(debug_name, RTLD_NOW|RTLD_LOCAL, &extinfo)` with
     `extinfo.flags = ANDROID_DLEXT_USE_LIBRARY_FD` — called **unconditionally, no API-level
     guard** (this flag carries no version gate). **This is the real load** — Bionic's linker maps
     `PT_LOAD` segments straight out of the memfd's pages and does everything else (`DT_NEEDED`
     resolution, relocations, TLS, running constructors) exactly as it would for a path-based `.so`.

On any failure (old API level, or a non-Android desktop build), `GameHub`'s loader falls back to
writing the bytes read-only to disk and calling the ordinary `dlopen(path)` path.

### 2.2 A from-scratch ELF loader was researched *and explicitly rejected* in this exact codebase

`GameHub/research/11-secure-loader-and-in-memory-module-handling.md` originally proposed a
hand-rolled userland ELF loader (manual `PT_LOAD` mapping, manual relocation processing, a hybrid
symbol resolver, manual `.init_array` execution). A correction box at the top of that document
states plainly:

> "This is, in substance, the same technique `Progress/architecture.md` §8.5 already documents as
> researched and abandoned in this exact project after being blocked twice by automated safety
> review, for being 'mechanically identical to reflective/manual-mapping malware loaders,
> independent of stated intent.' ... Do not implement any part of this, including a partial/
> scoped-down version."

Only the **loading mechanism itself was kept as memfd + `android_dlopen_ext`** (fully OS-linker
registered — `dl_iterate_phdr()` and `/proc/pid/maps` both see it normally, just with no on-disk
directory entry); what was abandoned was replacing the linker's own ELF-parsing/relocation
machinery with hand-rolled code.

**Direct implication for Konative**: "pack a self-contained `.so` and load it directly from
memory, no disk artifact" is fully achievable — and was *shipped* in this exact reference repo —
purely by feeding bytes into `memfd_create()` + `android_dlopen_ext(ANDROID_DLEXT_USE_LIBRARY_FD)`
and letting Bionic's real linker do everything else. Konative should adopt this same boundary: no
custom ELF/relocation engine, ever. If Konative's own design impulse trends toward "let's write our
own loader for full control," this prior art is direct evidence that a team building almost exactly
this went down that road, got blocked by their own safety review, and backed out — worth treating
as a hard constraint rather than a preference.

Supporting technical detail from the same research corpus (skimmed, not re-verified independently
by this report, but internally cross-checked against Bionic source references in the documents
themselves):
- `memfd_create` needs **API 30** on Bionic; `android_dlopen_ext`/`ANDROID_DLEXT_USE_LIBRARY_FD`
  themselves are present since **API 24**.
- Android's "Safer Dynamic Code Loading" enforcement (blocking writable dex loads) is a **path**-based,
  Java-level (`Runtime.load0()`/`File.canWrite()`) check with no fd-based code path — it categorically
  cannot fire against a direct native `android_dlopen_ext` call, since that never goes through
  `System.load()`.
- A memfd-loaded library is **fully visible** in `/proc/pid/maps` as `/memfd:<name> (deleted)` and to
  `dl_iterate_phdr()` — this technique removes only the *on-disk, file-copy-discoverable* artifact,
  not live-memory-residency (a rooted device with `/proc/pid/mem` access still sees everything).
- SELinux grants `execmem` to all app domains on stock AOSP (`allow appdomain self:process execmem`
  in `app.te`) — a hand-rolled loader mapping anonymous RX pages is *not* blocked by SELinux, which is
  exactly why it was technically feasible and exactly why its rejection here was a policy decision,
  not a "we couldn't get it working" one.

---

## 3. CMake/build-tooling architecture: corrosion as the model

[corrosion-rs/corrosion](https://github.com/corrosion-rs/corrosion) is the closest existing analog
to "make a foreign toolchain a first-class CMake citizen," and its concrete design choices map
almost one-to-one onto what a `konative_import_module()`-style CMake API needs.

### 3.1 Module layout

- `Corrosion.cmake` — public API (`corrosion_import_crate`, `corrosion_link_libraries`,
  `corrosion_set_*`, `corrosion_add_cxxbridge`, `corrosion_install`, ...).
- `CorrosionGenerator.cmake` — the actual `cargo metadata` → CMake-target translation logic.
- `FindRust.cmake` — toolchain discovery + Android/Windows/OHOS target-triple derivation.
- `CorrosionConfig.cmake.in` — standard `find_package()`-able package config template.

**Recommendation: mirror this exact split** — one umbrella `Konative.cmake` (public API), one
`KonativeGenerator.cmake` (the actual `kotlinc`/`d8` orchestration), one `FindKotlin.cmake` (JDK /
kotlinc / d8 / android.jar discovery), one `KonativeConfig.cmake.in`.

### 3.2 How `corrosion_import_crate()` actually works, and what transfers

1. Shells out to `cargo metadata` (real tool, not reimplemented) to discover crates/targets.
2. For each importable target (only `bin`, `staticlib`, `cdylib` — plain `rlib` is skipped, since
   it isn't externally linkable without further glue), builds one `add_custom_target` +
   `add_custom_command` invoking `cargo rustc` directly — **not** `ExternalProject_Add**. Cargo's
   own incremental cache, not CMake, decides whether a rebuild is actually needed.
3. **Predicts** artifact filenames from target name/crate-type/platform convention rather than
   globbing the output directory after the fact.
4. Copies predicted artifacts into CMake's standard per-target output dirs
   (`ARCHIVE_OUTPUT_DIRECTORY` etc., including `_<CONFIG>` variants) via a `POST_BUILD`
   `copy_if_different` step with declared `BYPRODUCTS`, so Ninja/Make treat it as a real cacheable
   build edge.
5. Because output-directory *properties* can be set on the imported target **after**
   `corrosion_import_crate()` returns, actual `IMPORTED_LOCATION` assignment is deferred via
   `cmake_language(DEFER CALL ...)` until end-of-directory-processing, so later user overrides
   still take effect.
6. **Two-layer target model**: a public `INTERFACE` library (named after the crate's `[lib]` name,
   what user code `target_link_libraries()`s against) wraps an internal, hidden
   `IMPORTED GLOBAL` target (`<name>-static`/`<name>-shared`) actually carrying the real artifact —
   so the embedding/linking strategy underneath can change without breaking the public target name.
7. **Cross-compilation**: `FindRust.cmake` reads the NDK toolchain file's own
   `CMAKE_ANDROID_ARCH_ABI` variable and maps it straight to a Rust target triple
   (`arm64-v8a`→`aarch64-linux-android`, etc.) — it does **not** re-derive ABI/API-level selection
   itself, it piggybacks entirely on variables the NDK toolchain file already established. Same
   pattern for Windows (`CMAKE_VS_PLATFORM_NAME`) and OpenHarmony.
8. **Multi-config generators** (Visual Studio/Xcode): build flags are threaded through as
   `$<CONFIG>`-based generator expressions, and artifacts are placed in a `$<CONFIG>` subdirectory —
   a late, painful fix in corrosion's own history per its CHANGELOG, worth doing right from the
   start.
9. **Linking split by artifact kind**: for a `staticlib`, CMake's own linker performs the final
   link (Rust's static lib is just another `.a`); for `cdylib`/`bin`, **`rustc` itself performs the
   final link**, so any C/C++ library the Rust side depends on must be forwarded via
   `corrosion_link_libraries()` (passes `-l`/`-L` into the linker invocation via rustflags). This
   is largely **moot for Konative** if the embedded-dex approach is chosen (dex embedding is a data
   blob, not a native link step) — but resurfaces immediately if Konative also supports the
   Kotlin/Native AOT path, where `kotlinc-native -produce static` really does need this same
   "who performs the final link" reasoning (see 3.4).

Source: [corrosion-rs.github.io/corrosion](https://corrosion-rs.github.io/corrosion/),
[Usage](https://corrosion-rs.github.io/corrosion/usage.html),
[Advanced](https://corrosion-rs.github.io/corrosion/advanced.html), raw module source at
[`Corrosion.cmake`](https://raw.githubusercontent.com/corrosion-rs/corrosion/master/cmake/Corrosion.cmake) /
[`FindRust.cmake`](https://raw.githubusercontent.com/corrosion-rs/corrosion/master/cmake/FindRust.cmake) /
[`CorrosionGenerator.cmake`](https://raw.githubusercontent.com/corrosion-rs/corrosion/master/cmake/CorrosionGenerator.cmake).

### 3.3 Existing prior art doing almost exactly the dex/d8 half of this already

[`kautils/CMakeAndroidD8`](https://github.com/kautils/CMakeAndroidD8) — a single-file CMake
module wrapping `d8` invocation:

```cmake
CMakeAndroidD8(
    D8 "path_to_d8"  RELEASE|DEBUG
    CLASSPASS "android.jar" "./libs/*"
    DESTINATION .  FILES ${class_jars} ${lib_jars}
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"  REQUIRED
)
```

This is direct proof the "wrap a foreign CLI toolchain step as a CMake function producing a real
build artifact" pattern transfers cleanly from Cargo to `d8`. `GameHub/cmake/modules/JvmDex.cmake`
(section 1.2) is a more complete version of the same idea (adds the `kotlinc`/`javac` compile
stage and the binary-embedding stage on top). Konative's own module should be built as a
generalization of this lineage, not from scratch.

### 3.4 Other integration philosophies, and where each is/isn't a fit

Three genuinely distinct strategies exist across the ecosystem, useful for scoping which parts of
Konative should use which:

| Strategy | Examples | Fit for Konative |
|---|---|---|
| **Delegate to the real foreign toolchain via custom-command orchestration** | corrosion (`cargo rustc`), `CMakeAndroidD8` (`d8`), `GameHub/JvmDex.cmake` (`kotlinc`+`d8`) | **Best fit** for the dex/bytecode-compile pipeline — never reimplement `kotlinc`/`d8` semantics |
| **Reimplement the foreign build orchestration inside the host build system** | Meson's Cargo-wrap subprojects (compiles Rust via Meson's own Rust module, not `cargo build`) | Higher control, higher maintenance burden, real documented gaps (Meson's Cargo wraps don't yet correctly set `OUT_DIR`/build-script env vars) — **avoid** for Konative |
| **Pure source-generation, build-system-agnostic** | SWIG, Djinni, jextract (generate bindings, consumer feeds them into whatever build system) | Good model for any Kotlin↔C++ **call-boundary bridging** codegen Konative needs, separate from the bulk dex-embedding data path |

`cxx` (the Rust↔C++ interop crate) is worth naming for a specific reason: it has **no officially
endorsed CMake integration** — its own docs list five divergent community setups and call none of
them official. That's a real gap Konative should not leave open for Kotlin — a first-party,
maintained CMake module (per this report's whole thrust) is the differentiator corrosion has and
cxx-on-CMake lacks. Source: [cxx.rs/build/cmake.html](https://cxx.rs/build/cmake.html).

**Mozilla's UniFFI** is worth citing for one architectural idea specifically: it cleanly separates
"produce the native binary" (plain `cargo build` over generated FFI scaffolding) from "produce the
per-language binding/loader code" (`uniffi-bindgen generate ... -l kotlin`, one sub-module per
target language). Applied to Konative: the "compile Kotlin → embed as data" stage and the "trivial
Java/Kotlin shell that calls `loadLibrary()`" stage are similarly separable concerns, and keeping
them as two distinct pieces of tooling (rather than one monolith) mirrors UniFFI's design.
Source: [mozilla/uniffi-rs](https://github.com/mozilla/uniffi-rs).

### 3.5 Android NDK CMake toolchain mechanics (the ground truth Konative builds on top of)

- Toolchain file: `<NDK>/build/cmake/android.toolchain.cmake`, passed via
  `-DCMAKE_TOOLCHAIN_FILE=...`.
- `ANDROID_ABI` (required, one of `armeabi-v7a`/`arm64-v8a`/`x86`/`x86_64`) — **one ABI per CMake
  configure**; multi-ABI builds are separate configure/build passes (this is what Gradle's
  `externalNativeBuild` does under the hood, once per ABI).
- `ANDROID_PLATFORM` sets the min API level (mirrors `minSdkVersion`).
- `ANDROID_STL` (`c++_shared` default / `c++_static` / `none` / `system`) — **directly relevant to
  the "single `.so`" goal**: `c++_static` avoids needing a second `libc++_shared.so` shipped
  alongside the main binary, at the cost of a larger single binary — almost certainly the right
  choice for Konative's "one `.so`, nothing else" goal.
- All of `ANDROID_ABI`/`ANDROID_PLATFORM`/`ANDROID_NDK` are readable back out of the CMake cache
  after `project()` — exactly what a Konative module should read to auto-derive `d8 --min-api`
  and the correct `android.jar` path, mirroring corrosion's `FindRust.cmake` pattern exactly (3.2
  point 7).

Source: [Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake).

### 3.6 Embedding the compiled blob into the `.so`: four real mechanisms, one clear winner

| Mechanism | How | Trade-off |
|---|---|---|
| `xxd -i`-style C-array generation | Text-generate a `.c`/`.h` with a byte-array literal | Simple, portable everywhere, but **compiler chokes on multi-MB generated source** (a multi-MB dex becomes a multi-MB source file to lex/parse) — bad fit for a whole `classes.dex` |
| `objcopy -I binary -O <elf-fmt>` / `ld -r -b binary` | Directly produces a linkable `.o` exposing `_binary_<name>_start/_end/_size` symbols, no intermediate source | Single build step, but symbol names derive from the input filename/path (reproducibility gotcha), and needs correct `-B`/architecture flags matched to the NDK target |
| GAS `.incbin` directive | A tiny hand-authored/generated `.S` file `.incbin`s the blob directly, assembled by the **same** Clang/LLD invocation as the rest of the sources | Single-pass, composes trivially with normal CMake source-dependency tracking (as long as the generated `.S`'s custom command lists the blob as a dependency), fully portable to NDK's Clang/LLD — **best default for Android/ELF targets specifically** |
| `file(GENERATE)` + custom command chain | CMake orchestration only; still needs one of the above as the actual embedding step | Not itself a mechanism — it's the CMake glue that chains `.kt→.class→.dex→.o/.S` as real build-graph edges with correct dependency tracking |

`GameHub`'s own `bin_to_c_array.py` (section 1.2) is literally the first row of this table (C-array
generation) — it works for `GameHub`'s per-module dex blobs (each a few MB) but would scale poorly
to one large monolithic blob; `.incbin` is the recommended upgrade path if Konative's blob ends up
significantly larger or if compile time on the generated source becomes a measured problem.

Sources: [devever.net — incbin](https://www.devever.net/~hl/incbin),
[Laurence Tratt — most portable way to embed binary blobs](https://tratt.net/laurie/blog/2022/whats_the_most_portable_way_to_include_binary_blobs_in_an_executable.html).

---

## 4. `GameHub`'s cross-module capability registry (optional architecture reference)

Not required for Konative's core goal, but directly relevant if Konative ever wants **more than
one** independently-buildable native module composed into the final `.so`/process (e.g. a
rendering module and an app-logic module that shouldn't statically link against each other):

- Every payload receives one flat function-pointer table (`GameHubHostApi`) at load time from the
  Loader — every cross-module interaction is mediated through it, never direct linkage.
- Two lookup mechanisms: `resolve_foreign_symbol` (classic `dlsym`-by-exported-name — simple, but
  the exported name is necessarily visible in `.dynsym`, readable via `readelf --dyn-syms` by
  anyone with the built `.so`) and a **capability registry** (a provider hands the Loader a raw,
  possibly `static`/internal-linkage function pointer directly, in-process, at load time, keyed by
  a compile-time-shared `uint32_t` id — never touches `.dynsym` at all). `GameHub`'s own current
  guidance: "use the capability registry for anything new."
- Resolution and "pinning" (incrementing an in-flight-call counter so the target can't be torn down
  mid-call) happen as one atomic operation under a single lock — no window where a resolved
  pointer's target could start retiring between "found it" and "protected it."
- Deliberately **zero runtime signature verification** — casting the resolved `void*` to a
  concrete function-pointer type is a compile-time-reviewed decision, not an adversarial-boundary
  check, because every module in the process came from the same build/signing pipeline.

Full reference: [`GameHub/CROSS_MODULE_API.md`](../../../GameHub/CROSS_MODULE_API.md) (31KB, the
canonical write-up) and `GameHub/libs/payload_abi/`.

---

## 5. Prior art: who else unifies native + managed-code builds

No project found does *exactly* what Konative proposes (a general, openly-documented CMake
framework for "one `.so`, embedded managed bytecode, native rendering + logic"). The closest
adjacent, shipped architectures:

| Project | Mechanism | Relevance |
|---|---|---|
| **Xamarin / Mono ("Bundle assemblies into native code")** | GZip-compressed **IL** (not native code) for every managed assembly, embedded directly in `libmonodroid_bundle_app.so`'s data segment; decompressed and handed to an embedded Mono interpreter/JIT at startup | **Closest direct precedent for the dex-embedding path** — literally "bytecode as a data blob riding inside an ELF `.so`." Notably, Microsoft **partially walked this back** in modern .NET-for-Android/MAUI in favor of separate per-assembly `.so` files with shared LZ4 compression, for size/build-efficiency reasons — a real cautionary data point against a single giant embedded blob. |
| **Flutter engine** | `gen_snapshot` produces AOT machine-code + data snapshots; default Android packaging ships them as separate files, but Flutter **already has and documents** an `aot-shared-library-path` flag that fuses all snapshot artifacts into one `.so` via a host-side native toolchain step | Direct proof "fuse everything into one `.so`" is a solved, shipped problem for an AOT-compiled managed runtime — but per-ABI, since AOT snapshots are architecture-specific (analogous to needing per-ABI dex-embedding runs). |
| **React Native / Hermes** | Compiles JS to Hermes bytecode (HBC) — but ships it as a **plain separate asset file** in the APK, not fused into `libhermes.so` at all | Architecturally the **opposite** choice from Flutter's fusion, and it's simpler *because* HBC bytecode is architecture-independent (only the interpreter engine `.so` is per-ABI) — no per-ABI blob-matching problem to solve. **This is the closer analogy if Konative's dex blob is genuinely architecture-independent JVM bytecode** (which it is) rather than AOT machine code. |
| **Kotlin/Native** | Compiles Kotlin straight to native machine code (LLVM-based); can produce a real loadable `.so`/`.dylib` via `binaries { sharedLib() }` | This is **the mechanism Konative's own current README already commits to**. It sidesteps dex/ART entirely — no JVM bytecode exists at any point — at the cost of Kotlin/Native's different (non-JVM) standard library and no direct access to Android Framework Java APIs the normal JVM-target Kotlin has. Already fully shipped/solved by JetBrains; `GameHub/cmake/modules/KotlinNative.cmake` is a working, real CMake wrapper around exactly this compiler for the specific case of `-target android_arm64`-style AOT builds (see section 6). |
| **Godot** (`libgodot.so`) / **Unity** (`libunity.so` + `libmono.so`/`libil2cpp.so`) | Single/few core native `.so`s, but scripting logic (GDScript/C#/Mono, or Unity's C# via Mono or IL2CPP-transpiled-to-C++) still ships as **separate** payloads, not dex-embedded | Confirms single-`.so`-core-engine is a well-trodden shape; neither engine currently embeds managed bytecode *inside* that core `.so` the way Konative proposes — a genuine gap Konative would be filling, not duplicating. |

Sources: Xamarin — [dotnet/android #4527](https://github.com/dotnet/android/issues/4527),
[#8659](https://github.com/dotnet/android/issues/8659),
[Mono-Rebundle](https://github.com/met94-zz/Mono-Rebundle),
[.NET MAUI runtimes docs](https://learn.microsoft.com/en-us/dotnet/maui/deployment/runtimes-compilation?view=net-maui-10.0).
Flutter — [engine AOT-mode doc](https://github.com/flutter/engine/blob/main/docs/Flutter-engine-operation-in-AOT-Mode.md).
Hermes — [React Native Hermes docs](https://reactnative.dev/docs/hermes),
[Payatu — Understanding and Modifying Hermes Bytecode](https://payatu.com/blog/understanding-modifying-hermes-bytecode/).
Kotlin/Native — [Dynamic library tutorial](https://kotlinlang.org/docs/native-dynamic-libraries.html).

---

## 6. `GameHub`'s *other* Kotlin path: `KotlinNative.cmake` (the AOT alternative, already working)

Because Konative's current README points at the AOT path, this deserves its own section — it is
**not** the same code as `JvmDex.cmake` and solves a different problem.

`GameHub/cmake/modules/KotlinNative.cmake` invokes the standalone **Kotlin/Native compiler**
(`kotlinc-native` — a completely separate distribution from JVM-targeting `kotlinc`; the two ship
as different release zips on JetBrains' GitHub and must not be confused) to compile `.kt` sources
**ahead-of-time straight to native machine code**, producing a `.a` static archive with zero
JVM/dex/ART involvement at runtime.

- `gamehub_kotlin_native_target_for_abi()` maps the project's NDK `ANDROID_ABI` to kotlinc-native's
  own lower-level target names (`android_arm64`, `android_arm32`, `android_x64`, `android_x86` —
  **not** the Gradle/KMP-facing names like `androidNativeArm64`; using the wrong name fails with
  "Unknown target").
- `gamehub_kotlin_native_static_lib(name KOTLIN_SRC_DIR <dir> [CINTEROP_DEF <file>])`: optionally
  runs `cinterop` for C headers not already covered by Kotlin/Native's pre-built platform klibs
  (`platform.gles2`/`egl`/`gles3` ship pre-built for every `android_*` target already); invokes
  `kotlinc-native <sources> -produce static -target <target> [-opt|-g] -o <name>`; wraps the result
  as `IMPORTED GLOBAL` and aliases it `gamehub::kotlin::<name>` — **the identical CMake idiom**
  `JvmDex.cmake` uses for `gamehub::jvmdex::<name>`, so both mechanisms present identically to a
  consumer.
- Empirically-found gotchas worth inheriting: `@CName`-exported symbols survive into `.dynsym` even
  under `-fvisibility=hidden` + `--strip-unneeded`, with zero extra effort required; Kotlin/Native's
  Android runtime calls `__android_log_print` internally, so `-llog` is needed at final link time —
  undiscovered except via a real link failure; any `platform.gles2`/`egl` symbol usage requires the
  **consumer** to separately link the matching system stub (`-lGLESv2` etc.), since the platform
  klib wrapper is a thin stub, not a self-contained implementation.

Full source: [`GameHub/cmake/modules/KotlinNative.cmake`](../../../GameHub/cmake/modules/KotlinNative.cmake).

**Why `GameHub` has both**: `JvmDex.cmake` exists because its Kotlin **UI** code (real
`Dialog`/`View`/`WindowManager` overlays) genuinely needs the Android UI framework's live JVM
object model — that only exists inside ART, and you cannot draw a real Android `View` tree from
AOT-compiled native code without reimplementing huge parts of the framework yourself.
`KotlinNative.cmake` exists for Kotlin code that wants the *language* (nullability, expressive
control flow, etc.) for logic that talks directly to GLES/EGL, without paying any JVM/ART tax. They
are two independent tools for two different Kotlin use cases inside the same project — not
competing implementations of one idea.

---

## 7. The design fork, spelled out for a decision

| | **Dex embedding (JVM bytecode + ART)** | **Kotlin/Native (AOT machine code)** |
|---|---|---|
| What ships in the `.so` | Compiled `classes.dex` bytes as linked data | Real machine code, linked like any other object file |
| Runtime dependency | The host app's already-live ART VM (reused via `JNI_OnLoad`) | None — no JVM anywhere in the picture |
| Access to Android Framework APIs (View, Activity, WindowManager, Compose-style UI) | **Full, normal access** — it's real JVM code running in the real app process | **None directly** — Kotlin/Native has its own, different (non-JVM) standard library; framework UI would need a from-scratch native rendering path (this is what NativeActivity/GameActivity + EGL/GLES is for) |
| Size cost (verified) | ~2.5 MB dex for a near-trivial Kotlin object with `-include-runtime` and no tree-shaking (real number from `GameHub`'s own round-trip test) | Whatever the AOT-compiled code + Kotlin/Native's own (smaller, non-JVM) runtime costs — no separate stdlib-in-dex tax |
| Execution mode / performance ceiling | Generally **interpreted or JIT'd on demand** — dex loaded via `InMemoryDexClassLoader` is not normally `dex2oat`-precompiled the way installed-APK dex is, since it was never registered with the package manager. Cold-start of Kotlin-heavy logic loaded this way will be slower than an installed APK until JIT warms up — a real, currently-unsolved constraint the packer literature accepts rather than works around. | Real machine code from the moment the `.so` is mapped — no interpretation/JIT tier at all |
| Precedent already shipped in `GameHub` | `JvmDex.cmake` + `dex_loader.cpp` (this repo, used for its Kotlin **UI overlays** specifically) | `KotlinNative.cmake` (this repo, used for logic that talks to GLES/EGL without JVM overhead) |
| Closest external prior art | Xamarin/Mono's "bundle assemblies into native code" (bytecode-as-data-blob, later partially walked back for size reasons); the entire Android-packer industry (section 1.4) | Kotlin/Native's own official `binaries { sharedLib() }` (JetBrains-shipped, solved) |
| Konative's current README | Not what it currently describes | **This is what it currently describes** |

**This report does not pick one for you** — but it's worth being explicit that `GameHub` itself
didn't have to choose: it ships both, gated to different use cases within the same project
(JVM-hosted UI vs. AOT native logic). Konative could plausibly do the same — a
`konative_add_kotlin_native_module()` for AOT logic/rendering-adjacent code (matching the current
README) and a separate `konative_embed_kotlin_dex()` for anyone who wants real Android Framework
UI/Activity access from inside the same `.so` (matching the original chat framing and the bulk of
this research). If only one is in scope for v1, the README's already-stated Kotlin/Native direction
is the more novel, less-previously-done-as-obfuscation choice, and is fully consistent with
"Corrosion for Cargo, applied to Kotlin" as literally stated — corrosion's whole model (section 3)
maps directly onto wrapping `kotlinc-native` the same way it wraps `cargo rustc`.

---

## 8. Size and performance data points (what's actually known, quantitatively)

- **Kotlin stdlib jar itself**: ~1MB+ (per [Maven Central listing](https://mvnrepository.com/artifact/org.jetbrains.kotlin/kotlin-stdlib)).
  After dexing + R8 shrinking in a normal Gradle build, only reachable methods survive (e.g. Jetpack
  Compose alone reported to add ~1MB of dex in a real shrunk build) — but Konative's dex-embedding
  path as currently prototyped in `GameHub` does **not** shrink (`-include-runtime`, no R8 pass), so
  the unshrunk cost is the relevant number: **~2.5MB for a near-trivial Kotlin object**, empirically
  measured in `GameHub`'s own build.
- **Real packer overhead, for comparison**: DexHunter's evaluation found Bangcle's packer adds
  "more than 600KB of additional data" per packed app — i.e. commercial packers doing the same
  category of thing land in the same low-single-digit-MB range this report's other numbers suggest.
- **No clean split-APK-vs-single-.so percentage benchmark exists** in the literature surveyed —
  the numbers above (⁓1MB stdlib, ⁓600KB-2.5MB per-blob overhead) are the best available proxies,
  and suggest a realistic floor of **low-single-digit-MB overhead** for a non-trivial embedded
  Kotlin app's dex plus a lightweight native loader stub, before adding whatever native
  rendering/logic code sits alongside it.
- **The bigger performance risk is not size, it's execution mode** (see the table in section 7):
  dex loaded via `InMemoryDexClassLoader` is not normally AOT-precompiled by the system the way
  installed-app dex is. This is a real, currently-undocumented-as-solved constraint of the
  dex-embedding path specifically — Konative should state it plainly in its own docs rather than
  imply it's solved, if that path is pursued.

---

## 9. Sketch: what a Konative CMake API could look like (synthesis, not a commitment)

Combining corrosion's target-model idioms (section 3.2) with the two build paths (section 7):

```cmake
# Path A — dex embedding (JVM-hosted UI/logic), mirrors GameHub's JvmDex.cmake
konative_embed_kotlin_dex(
    app_kt
    KOTLIN_SRC_DIR src/kotlin/
    MIN_API 26                 # floor imposed by InMemoryDexClassLoader itself
    # internally: kotlinc -include-runtime -jvm-target 1.8 -cp android.jar -> jar
    #             d8 --min-api 26 -> classes.dex
    #             embed via .incbin (not bin_to_c_array, for the size reasons in §3.6)
)
add_library(app_native SHARED main.cpp jni_bridge.cpp)
target_link_libraries(app_native PUBLIC gamehub::jvmdex::app_kt)  # -style ALIAS target

# Path B — Kotlin/Native AOT (matches Konative's current README), mirrors KotlinNative.cmake
konative_add_kotlin_native_module(
    render_kt
    KOTLIN_SRC_DIR src/kotlin-native/
    CINTEROP_DEF gles.def       # optional, only for headers not already in prebuilt platform klibs
)
target_link_libraries(app_native PUBLIC konative::native::render_kt)
```

Design principles carried over directly from corrosion (section 3.2/3.6), restated as a checklist:

1. **Delegate to the real toolchain binaries** (`kotlinc`/`kotlinc-native`/`d8`) via
   `add_custom_command`/`add_custom_target` — never reimplement compiler semantics in CMake.
2. **Predict artifact paths deterministically** rather than globbing after the fact.
3. **Two-layer target model** (public `INTERFACE`/`ALIAS` wrapping a hidden `IMPORTED` target) so
   the embedding mechanism can change later without breaking the public target name.
4. **Deferred `IMPORTED_LOCATION`** via `cmake_language(DEFER CALL ...)` if output-directory
   properties might be set after the import call.
5. **Read the NDK toolchain file's own cache variables** (`ANDROID_ABI`/`ANDROID_PLATFORM`/
   `ANDROID_NDK`) rather than re-deriving ABI/API-level logic — exactly what `FindRust.cmake` and
   `GameHub`'s own `JvmDex.cmake`/`KotlinNative.cmake` both already do.
6. **Generator-expression-aware from day one** if Debug/Release ever need different `d8`/kotlinc
   flags (e.g. an obfuscated/shrunk Release dex vs. a plain Debug one).
7. **A first-party, maintained CMake module**, not a "someone made one that works" community
   afterthought — this is explicitly the gap `cxx` has left open on the Rust side (section 3.4) that
   Konative can avoid leaving open for Kotlin.

---

## 10. Consolidated source list

**Dex embedding / packers (section 1):**
[InMemoryDexClassLoader](https://developer.android.com/reference/dalvik/system/InMemoryDexClassLoader) ·
[DexClassLoader](https://developer.android.com/reference/dalvik/system/DexClassLoader) ·
[huaxiaozhou/android-protection load.c](https://github.com/huaxiaozhou/android-protection/blob/master/LoadDex/DexLoaderJni/jni/load.c) ·
[Gyoonus/android_dynamic_loader](https://github.com/Gyoonus/android_dynamic_loader) ·
[AWAKE — Android Packers & Obfuscators](https://zahidaz.github.io/awake/packers/) ·
[DexHunter (PDF)](https://www4.comp.polyu.edu.hk/~csxluo/DexHunter.pdf) ·
[AppSpear (PDF)](https://lijuanru.com/publications/jss18.pdf) ·
[arXiv 1611.10231 — obfuscation survey](https://arxiv.org/pdf/1611.10231) ·
["Practical Android Software Protection in the Wild" (ACM)](https://dl.acm.org/doi/10.1145/3757735) ·
[arXiv 2509.16340 (2026)](https://arxiv.org/html/2509.16340v1) ·
[DexProtector](https://licelus.com/products/dexprotector) ·
[Appdome dex/control-flow relocation](https://www.appdome.com/how-to/mobile-app-security/mobile-code-obfuscation/dex-control-flow-relocation-anti-reversing-for-android-apps/) ·
[DexGuard](https://www.guardsquare.com/dexguard) ·
[Caleb Fenton — JVM from native code](https://calebfenton.github.io/2017/04/05/creating_java_vm_from_android_native_code/) ·
[Yrom — standalone Java+JNI via app_process](https://yrom.net/blog/2023/07/07/run-java-with-jni-app-on-android/).

**CMake/build tooling (section 3):**
[corrosion-rs/corrosion](https://github.com/corrosion-rs/corrosion) ·
[corrosion docs](https://corrosion-rs.github.io/corrosion/) ·
[kautils/CMakeAndroidD8](https://github.com/kautils/CMakeAndroidD8) ·
[cxx.rs CMake page](https://cxx.rs/build/cmake.html) ·
[mozilla/uniffi-rs](https://github.com/mozilla/uniffi-rs) ·
[Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake) ·
[devever.net — incbin](https://www.devever.net/~hl/incbin) ·
[Laurence Tratt — embedding binary blobs](https://tratt.net/laurie/blog/2022/whats_the_most_portable_way_to_include_binary_blobs_in_an_executable.html).

**Prior-art frameworks (section 5):**
[dotnet/android #4527](https://github.com/dotnet/android/issues/4527) ·
[Mono-Rebundle](https://github.com/met94-zz/Mono-Rebundle) ·
[.NET MAUI runtimes docs](https://learn.microsoft.com/en-us/dotnet/maui/deployment/runtimes-compilation?view=net-maui-10.0) ·
[Flutter AOT-mode doc](https://github.com/flutter/engine/blob/main/docs/Flutter-engine-operation-in-AOT-Mode.md) ·
[React Native Hermes docs](https://reactnative.dev/docs/hermes) ·
[Kotlin/Native dynamic library tutorial](https://kotlinlang.org/docs/native-dynamic-libraries.html).

**Local codebase (all sections; paths relative to the sibling `GameHub` repo checkout):**
`cmake/modules/JvmDex.cmake` · `cmake/modules/KotlinNative.cmake` ·
`cmake/scripts/bin_to_c_array.py` · `libs/jni/include/gamehub/jni/dex_loader.hpp` ·
`libs/jni/src/dex_loader.cpp` · `libs/dynload/src/library_handle.cpp` ·
`libs/payload_abi/` · `CROSS_MODULE_API.md` · `loader/src/loader.cpp` ·
`loader/src/generation_manager.cpp` · `Module/README.md` · `Module/Login/README.md` ·
`Module/Menu/README.md` · `testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java` ·
`research/08-memfd-based-library-loading.md` ·
`research/11-secure-loader-and-in-memory-module-handling.md` ·
`research/07-elf-memory-hiding-and-anti-dump.md` ·
`research/05-loader-network-queue-and-safe-unload-api.md`.
