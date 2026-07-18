# Konative — `.incbin` Binary-Blob Embedding: Concrete CMake Recipe

Fills the concrete gaps left open by `research/research.md` section 3.6 ("Embedding the compiled
blob into the `.so`: four real mechanisms, one clear winner"), which already established *that*
GAS `.incbin` is the right default over C-array text generation for a multi-MB `classes.dex` blob,
citing [devever.net's incbin article](https://www.devever.net/~hl/incbin) and
[Laurence Tratt's blog post](https://tratt.net/laurie/blog/2022/whats_the_most_portable_way_to_include_binary_blobs_in_an_executable.html).
This document does not re-litigate that conclusion — it answers the five concrete implementation
questions research.md left open, verifies every claim against a primary source or real code (not
memory or a hypothetical Stack Overflow answer), and ends with ready-to-paste CMake.

Context this builds on, already read in full: `GameHub/cmake/scripts/bin_to_c_array.py` (48 lines,
the current C-array approach), `GameHub/cmake/modules/JvmDex.cmake` (237 lines, the
`kotlinc`/`d8`-to-blob pipeline this new mechanism plugs into as a drop-in replacement for the
final embedding step only), and `research/research.md` sections 1–3 (dex-embedding runtime
mechanism, corrosion-style CMake module architecture, NDK CMake toolchain mechanics).

---

## 1. The exact GAS directives, verified across three independent sources

Three sources — [Sourceware/GNU binutils' own `Type` directive docs](https://sourceware.org/binutils/docs/as/Type.html),
the real, widely-used [`graphitemaster/incbin`](https://github.com/graphitemaster/incbin) project's
actual generated-assembly source, and an [official ARM Developer blog post](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/useful-assembler-directives-and-macros-for-the-gnu-assembler)
— independently converge on the same set of ARM-vs-x86 divergences, which is what makes the
recipe below defensible rather than guessed:

| Directive | The gotcha | Source |
|---|---|---|
| `.type sym, @object` vs `.type sym, %object` | `@` is a **comment character** on ARM/AArch64 GAS dialects, so `.type`'s `@object`/`@function` form is unreliable there; ARM conventionally wants `%object`/`%function` instead. | Sourceware: "some of the characters used ... such as `@` ... are comment characters for some architectures"; ARM blog: "Most of the time we just use `%function` and `%object`"; `incbin.h` itself branches `%object` under `__arm__`, `@object` otherwise (fetched directly from `graphitemaster/incbin/blob/main/incbin.h`). |
| `.align N` | **Not portable**: on ARM this is a **power-of-two exponent** (`1 << N` bytes); on x86 GAS it's traditionally a **plain byte count**. Using `.align 4` gives 16-byte alignment on arm64 and 4-byte alignment on x86_64 from the *identical* source line. | ARM Developer blog, verbatim: "`.align` will align the location counter on a `(1 << argument)` byte boundary." |
| `.section NAME` with a non-well-known name | GAS only auto-assigns sensible default flags (alloc/writable/executable) to a short list of **exact** well-known names (`.text`, `.rodata`, `.data`, `.bss`); any other name — including a per-blob unique name like `.rodata.konative.<symbol>` — silently gets no default flags unless you spell them out. | ARM Developer blog, verbatim: "section names other than `.text`, `.rodata`, `.data` and `.bss` needs extra arguments in order to work as you expect." |

**The recipe below sidesteps all three** rather than picking a sigil I can't fully verify against
Clang's integrated assembler for both target triples:

- Never emits `.type` at all — it's pure ELF symbol-table metadata (STT_OBJECT vs STT_NOTYPE),
  load-bearing for nothing this mechanism needs (C++ only ever takes these symbols' *addresses*).
- Uses `.balign N` (always a byte count, on every GAS-compatible target, ARM included) instead of
  the ambiguous `.align`.
- Always spells out `.section .rodata.konative.<symbol>,"a"` (flags given, type omitted — GAS
  defaults an ELF section's type to `SHT_PROGBITS` whenever flags are given and no type is,
  sidestepping the `@`/`%` question there too).

### PIC/PIE: verified to be a non-issue for this specific mechanism

Android has required position-independent *code* in every `.so` since forever (that's what makes
a shared library shared) and PIE *executables* since API 21 — but both constraints govern how the
compiler emits **relocatable code and pointers**. A `.incbin`'d blob is opaque bytes with **no
internal pointers of its own** for the linker to relocate (the dex file format itself only uses
relative, not absolute, internal offsets) — it just needs to be `SHF_ALLOC`, read-only, which
`"a"` (no `"w"`, no `"x"`) already guarantees. There is no separate PIC/PIE flag to pass for the
`.S` file itself; whatever `-fPIC`/`CMAKE_POSITION_INDEPENDENT_CODE` the rest of the target
already uses applies uniformly (Clang applies the same `--target=aarch64-linux-android<api>` /
`--target=x86_64-linux-android<api>` triple to every source in the target, `.S` included, once
`enable_language(ASM)` lets CMake drive it through the same per-target compile-flag machinery).

### One more real, genuinely ABI/tool-adjacent gotcha: missing `.note.GNU-stack`

Hand-written `.S` files don't get the compiler's automatic `PT_GNU_STACK` marking the way normal
`.cpp → .o` compilation does — omitting it can make some LLD configurations warn or error
("missing .note.GNU-stack section implies executable stack"). Cheap insurance: always emit
`.section .note.GNU-stack,""` at the end (empty flags = explicitly non-allocatable, the correct
marker; confirmed general ELF/LLD behavior, e.g. [llvm/llvm-project#57009](https://github.com/llvm/llvm-project/issues/57009)
and the [Red Hat write-up on these linker warnings](https://www.redhat.com/en/blog/linkers-warnings-about-executable-stacks-and-segments)).

### A Windows-host-specific gotcha, since Konative's dev machine is win32

GAS/LLVM's assembly string lexer treats backslash as an escape character in quoted strings (GNU
`as` manual: "to get special characters into a string you precede them with a backslash"). A raw
`C:\Users\Tonyt\...\classes.dex` path baked verbatim into `.incbin "..."` risks corruption the
moment a path component looks like an escape sequence. **Always normalize to forward slashes**
before emitting the path (CMake's `file(TO_CMAKE_PATH ...)`, not `TO_NATIVE_PATH`) — forward
slashes are accepted by `fopen()`/Clang's file-open path on Windows too, so this fully removes the
ambiguity rather than trying to get backslash-doubling exactly right.

### Size symbol: emit it in assembly, not just via C++ pointer subtraction

Both viable options research.md's prompt flagged are real: computing `<prefix>_end - <prefix>_start`
in C++ pointer arithmetic works fine, but emitting a dedicated `<prefix>_size` symbol (a
link-time-resolved constant, zero runtime cost: `.quad <prefix>_end - <prefix>_start`) keeps the
call site a plain `size_t` read, matching `GameHub/cmake/scripts/bin_to_c_array.py`'s existing
`k<Name>Size` shape — a closer drop-in replacement for anyone migrating off the C-array approach.
`.quad` (8 bytes) is safe and uniform for exactly Konative's two target ABIs (arm64-v8a and
x86_64 are both LP64 — `size_t`/`uint64_t` is 8 bytes on both); it would need to become `.long`
(4 bytes) if a 32-bit ABI (armeabi-v7a/x86) were ever added.

---

## 2. Build-graph wiring: the actual correct answer, and why the obvious one is wrong

The prompt asked three concrete alternatives; the answer is **none of them alone** — the correct
mechanism only becomes clear once you notice a subtlety: **the generated `.S` file's *text* never
needs to change when `classes.dex`'s bytes change.** Only the path string and symbol prefix are
substituted into it; the actual bytes are read by the assembler at *assemble* time via `.incbin`,
not baked into the `.S` file's own text. This reframes the real question from "how do I regenerate
a file when its input changes" to "how do I force *recompilation of an unchanged source file* when
something it reads at compile-time changes" — which is a different, narrower CMake problem.

- **`configure_file()`/`file(GENERATE)` at configure time — wrong tool.** Both only re-run when
  CMake itself re-configures. Making a `.dex` rebuild force a full CMake reconfigure just to notice
  a byte changed is heavy-handed and wrong for an iterative kotlinc→d8→relink loop.
  `CONFIGURE_DEPENDS` doesn't even apply here regardless — per the
  [CMake `add_custom_command` docs](https://cmake.org/cmake/help/latest/command/add_custom_command.html),
  that keyword is specific to `file(GLOB)`/`file(GLOB_RECURSE)` re-scanning, not to `configure_file()`.
- **`OBJECT_DEPENDS` — correct in spirit, but generator-limited.** Per the
  [official CMake `OBJECT_DEPENDS` docs](https://cmake.org/cmake/help/latest/prop_sf/OBJECT_DEPENDS.html),
  it is real and does exactly what's needed semantically (forces recompilation of an object file
  when a file the compiler doesn't itself know to depend on changes) — the docs' "largely obsolete"
  framing is specifically about its *original* use (generated C/C++ headers, now handled by
  automatic dependency scanning), which doesn't apply here: no C/C++/ASM dependency scanner looks
  inside a `.S` file for `.incbin "..."` references, so this exact use case is still exactly what
  `OBJECT_DEPENDS` is for. **The catch, stated explicitly in the same docs**: "Visual Studio
  Generators and the Xcode generator cannot implement such compilation dependencies" — silently
  ignored there, risking a stale `.o`. Since Android NDK builds (Android Studio's own
  `externalNativeBuild`, and any command-line `cmake --build`) always go through Ninja or Makefiles
  in practice, this gap is unlikely to bite Konative specifically — but it's a real, documented
  limitation, not a hypothetical one.
- **`add_custom_command(OUTPUT <the .S file itself> ... DEPENDS <blob>)` — the actually portable
  answer, adopted below.** Making the *generated `.S` file itself* the `OUTPUT` of a custom command
  that `DEPENDS` on the blob turns this into an ordinary build-graph edge every CMake generator
  understands (Ninja, Make, **and** Xcode/VS) — no reliance on the generator-limited
  `OBJECT_DEPENDS` property at all. The custom command's `COMMAND` re-runs a tiny `cmake -P` script
  (full text below) that rewrites the (cheap, few-line) `.S` file fresh every time; CMake's ordinary
  "object older than source" rule then reassembles it, and ordinary target-level dependencies
  relink. This is also why the recipe below does *not* use `configure_file()` at all for the `.S`
  generation — it uses `file(WRITE)` inside a build-time script instead.

```cmake
add_custom_command(
  OUTPUT "${_out_s}"
  COMMAND "${CMAKE_COMMAND}"
          "-DKONATIVE_BLOB_PATH=${ARG_BLOB}"
          "-DKONATIVE_BLOB_SYMBOL=${ARG_SYMBOL}"
          "-DKONATIVE_OUT_FILE=${_out_s}"
          -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
  DEPENDS "${ARG_BLOB}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
  COMMENT "konative: regenerating ${ARG_SYMBOL}_blob.S (.incbin ${ARG_BLOB})"
  VERBATIM)
target_sources(${TARGET_NAME} PRIVATE "${_out_s}")
set_source_files_properties("${_out_s}" PROPERTIES GENERATED TRUE LANGUAGE ASM)
```

If the blob itself is the `OUTPUT` of another `add_custom_command` earlier in the same pipeline
(e.g. `d8`'s own dex-producing step), this `DEPENDS` edge composes with it automatically — CMake
recognizes a `DEPENDS` argument that matches another custom command's `OUTPUT` in the same
directory scope and wires the ordering in without an explicit `add_dependencies()`.

The generated `.S` file **must be listed as an ordinary source in `add_library()`/`target_sources()`
alongside the real `.cpp` files** — yes, exactly as research.md's own table implies ("assembled by
the same Clang/LLD invocation as the rest of the sources"). There is no separate object-file-import
step; CMake compiles it via its ASM language support in the same per-target compile/link graph as
everything else.

---

## 3. `enable_language(ASM)` — required, and NOT provided by the NDK toolchain file

Verified by direct inspection (fetched the live file): **the NDK's own
[`android.toolchain.cmake`](https://android.googlesource.com/platform/ndk/+/refs/heads/main/build/cmake/android.toolchain.cmake)
contains zero references to `CMAKE_ASM_COMPILER`, `CMAKE_ASM_FLAGS`, or ASM language setup of any
kind.** Konative's own top-level `CMakeLists.txt` must call `enable_language(ASM)` itself (or list
`ASM` in `project(... LANGUAGES C CXX ASM)`) — this is not automatic.

Once enabled, CMake's own `enable_language(ASM)` machinery defaults the ASM compiler to whatever
`CMAKE_C_COMPILER` already is — Clang, already correctly pointed at the right
`--target=aarch64-linux-android<api>`/`--target=x86_64-linux-android<api>` triple by the NDK
toolchain file for C/C++ — so the correct per-ABI target triple carries over to `.S` compilation
for free, no extra plumbing needed.

**Historical gotcha, now fixed, worth knowing about**: [android/ndk#1623](https://github.com/android/ndk/issues/1623)
documented CMake 3.19–3.22.1 combined with the NDK's ("despite the name") **recommended-by-default**
["legacy" toolchain mode](https://developer.android.com/ndk/guides/cmake) passing a malformed
`-gcc-toolchain` (missing `=`) flag specifically to the ASM compiler invocation, breaking `.S`
compilation while C/C++ compiled fine. Per the [NDK r23](https://github.com/android/ndk/wiki/Changelog-r23)/[r24](https://github.com/android/ndk/wiki/Changelog-r24)
changelogs, this was fixed at the NDK-toolchain-file level ("Fixed behavior of the legacy CMake
toolchain file when used with new versions of CMake (incompatible `-gcc-toolchain` argument)") — any
NDK r23 or later (a safe assumption for a 2026 project) needs no workaround.

---

## 4. A real, verified project doing exactly this

**[`graphitemaster/incbin`](https://github.com/graphitemaster/incbin)** is the mechanism itself,
real and in wide use — a header-only macro library implementing exactly the GAS-directive pattern
above (verified directly from `incbin.h`'s source), portable across GCC/Clang/ArmCC/other GAS-family
assemblers, with the exact ARM-vs-other `%object`/`@object` branch cited in section 1.

**[`official-stockfish/Stockfish`](https://github.com/official-stockfish/Stockfish)** — the world's
strongest open-source chess engine — vendors a copy of `incbin.h` at `src/incbin/incbin.h` and uses
it for **exactly** this task at a comparable scale to Konative's blob: embedding a multi-megabyte
binary (an NNUE neural-network evaluation file) directly into the compiled binary, real working
code, verified by direct fetch of `src/nnue/network.cpp`:

```c
#include "../incbin/incbin.h"
...
#if !defined(UNIVERSAL_BINARY) && !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUE, EvalFileDefaultName);
#endif
```
consumed later as `gEmbeddedNNUEData` / `gEmbeddedNNUESize` wrapped in a `MemoryBuffer`/`std::istream`
— structurally the same shape Konative's `InMemoryDexClassLoader` path needs (a `ByteBuffer` over
linked bytes, no disk I/O).

**Honest finding, not glossed over**: this exact mechanism, ported to **CMake on Android
specifically** (not Stockfish's native Makefile build), is real-world documented to have hit
friction — [Stockfish Discussion #4703](https://github.com/official-stockfish/Stockfish/discussions/4703),
"Error with nnue file while building stockfish 16 in android studio": `.incbin` failed with *"Could
not find incbin file 'nn-5af11540bbfe.nnue'"* under Android Studio's CMake `externalNativeBuild`,
traced to relative-path resolution not matching the assembler's actual working directory in that
build context. The community's common fix was to **disable** embedding (`-DNNUE_EMBEDDING_OFF`)
and load the network from APK assets at runtime instead, rather than fix the path. This is direct,
real-world evidence for exactly the mitigation this report's recipe already builds in: **always
resolve to an absolute path** (`get_filename_component(... ABSOLUTE)`) before it ever reaches the
`.incbin` string, never a relative one whose meaning depends on the assembler's ambient working
directory (which, per this real incident, is not guaranteed stable across CMake generators/IDEs).

---

## 5. `objcopy`/`llvm-objcopy` comparison — concrete, not hand-wavy

**Yes, `llvm-objcopy` is bundled with the NDK** — it ships inside the NDK's own Clang prebuilts
tree (confirmed via the [Android googlesource mirror listing it under `clang-r349610/bin/llvm-objcopy`](https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/refs/heads/android10-release/clang-r349610/bin/llvm-objcopy),
concretely at `<NDK>/toolchains/llvm/prebuilt/<host-tag>/bin/llvm-objcopy` in a real NDK install),
so no extra tool install is needed either way.

The concrete invocation, per [LLVM's own `llvm-objcopy` command guide](https://llvm.org/docs/CommandGuide/llvm-objcopy.html):

```bash
llvm-objcopy -I binary -B aarch64      -O elf64-littleaarch64 classes.dex classes_dex.arm64.o
llvm-objcopy -I binary -B i386:x86-64  -O elf64-x86-64        classes.dex classes_dex.x86_64.o
```

This does work, per-ABI, and is genuinely a single tool invocation with no `.S`/assembly step at
all. **The concrete reason to still prefer `.incbin`, beyond research.md's existing "single
Clang/LLD invocation, composes with normal CMake tracking" framing**: symbol naming.
`llvm-objcopy -I binary`'s output symbols are `_binary_<file_name>_start/_end/_size`, where
`<file_name>` is **the literal path string passed on the command line**, non-alphanumeric characters
(including `/`, `\`, `.`, `:`) mechanically replaced with `_`. Two concrete consequences:

1. An **absolute** path (needed for the exact same reproducibility reasons as section 1's Windows
   path discussion) produces an unwieldy, machine-path-dependent symbol name
   (`_binary_C__Users_Tonyt_..._classes_dex_start`) — different on every developer's machine/CI
   runner, unusable as a stable `extern "C"` declaration in hand-written C++.
2. Getting Konative's own chosen name (`<prefix>_start`) instead requires a **second** tool
   invocation, `llvm-objcopy --redefine-sym _binary_..._start=<prefix>_start ...`, adding a second
   moving part and a second place for a path-string mismatch to silently produce the wrong symbol
   name with no error (this is exactly the "reproducibility gotcha" research.md's own table already
   flagged, now made concrete).

With `.incbin`, Konative's own generated `.S` chooses the exact symbol name directly — one step,
no derived name, no rename pass. **Verdict: `.incbin` remains the better default for Konative's
specific case** (multi-MB blob, needs a stable chosen symbol name, already has a Clang/LLD build
graph) — but `llvm-objcopy -I binary` is a legitimate, simpler-for-a-one-off fallback worth knowing
about if a future use case ever needs to embed a blob **without** touching CMake's ASM language
support at all (e.g., a quick manual/scripted build outside the CMake graph).

---

## 6. Final recommendation: ready-to-paste CMake

Two files under `cmake/modules/`.

**`cmake/modules/KonativeEmbedBlob.cmake`:**

```cmake
# konative_embed_binary_blob(<target> BLOB <path> SYMBOL <prefix>)
#
# Embeds an arbitrary binary file as linked read-only data inside <target> via
# a GAS `.incbin` directive, assembled by the same NDK Clang/LLD invocation as
# <target>'s other sources - see research/incbin_embedding_research.md for the
# full rationale/citations behind every choice below (C-array text generation
# chokes lexing a multi-MB generated .cpp - research/research.md section 3.6;
# objcopy -I binary's symbol names are path-derived and unstable - section 5
# of the above).
#
# Exposes, for any TU to `extern "C"` declare:
#   extern const unsigned char <prefix>_start[];
#   extern const unsigned char <prefix>_end[];
#   extern const uint64_t      <prefix>_size;   // both target ABIs are LP64

include_guard(GLOBAL)

function(konative_embed_binary_blob TARGET_NAME)
  cmake_parse_arguments(ARG "" "BLOB;SYMBOL" "" ${ARGN})
  if(NOT ARG_BLOB)
    message(FATAL_ERROR "konative_embed_binary_blob(${TARGET_NAME}): BLOB <path> is required")
  endif()
  if(NOT ARG_SYMBOL)
    message(FATAL_ERROR "konative_embed_binary_blob(${TARGET_NAME}): SYMBOL <prefix> is required")
  endif()
  if(NOT TARGET ${TARGET_NAME})
    message(FATAL_ERROR
      "konative_embed_binary_blob(${TARGET_NAME}): no such target - call "
      "add_library()/add_executable() before this function")
  endif()

  # The NDK's own android.toolchain.cmake sets up NO ASM compiler variables at
  # all (verified directly against its source) - every consumer must enable
  # this itself. enable_language() is idempotent - safe to call from a
  # function, safe if called more than once across multiple blobs.
  enable_language(ASM)

  get_filename_component(ARG_BLOB "${ARG_BLOB}" ABSOLUTE)
  # Always forward-slash, even though we're on a Windows host generating for
  # an NDK target: GAS/LLVM's asm string lexer treats backslash as an escape
  # character, so a raw C:\Users\...\ path corrupts silently. See section 1.
  file(TO_CMAKE_PATH "${ARG_BLOB}" ARG_BLOB)

  set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/konative_embed/${TARGET_NAME}")
  set(_out_s "${_gen_dir}/${ARG_SYMBOL}_blob.S")
  file(MAKE_DIRECTORY "${_gen_dir}")

  # The generated .S is the OUTPUT of a command that DEPENDS on the blob -
  # portable across every CMake generator (Ninja/Make/Xcode/VS), unlike the
  # OBJECT_DEPENDS source-file-property alternative, which Xcode/VS silently
  # ignore per CMake's own docs. Regenerated at BUILD time (a `cmake -P`
  # script), not via configure_file()/file(GENERATE) at CONFIGURE time -
  # those only rerun when CMake itself reconfigures, the wrong trigger for
  # "only the .dex's bytes changed." See section 2 for the full reasoning.
  add_custom_command(
    OUTPUT "${_out_s}"
    COMMAND "${CMAKE_COMMAND}"
            "-DKONATIVE_BLOB_PATH=${ARG_BLOB}"
            "-DKONATIVE_BLOB_SYMBOL=${ARG_SYMBOL}"
            "-DKONATIVE_OUT_FILE=${_out_s}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    DEPENDS "${ARG_BLOB}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KonativeGenerateIncbinAsm.cmake"
    COMMENT "konative: regenerating ${ARG_SYMBOL}_blob.S (.incbin ${ARG_BLOB})"
    VERBATIM)

  target_sources(${TARGET_NAME} PRIVATE "${_out_s}")
  set_source_files_properties("${_out_s}" PROPERTIES GENERATED TRUE LANGUAGE ASM)
endfunction()
```

**`cmake/modules/KonativeGenerateIncbinAsm.cmake`:**

```cmake
# Build-time driver invoked via `cmake -P` from konative_embed_binary_blob()'s
# add_custom_command. Deliberately NOT a configure_file() .S.in template - see
# KonativeEmbedBlob.cmake / research/incbin_embedding_research.md section 2
# for why this must run at build time, not configure time.
foreach(_required KONATIVE_BLOB_PATH KONATIVE_BLOB_SYMBOL KONATIVE_OUT_FILE)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "KonativeGenerateIncbinAsm.cmake: ${_required} must be passed via -D")
  endif()
endforeach()

# No `.type sym, @object`/`%object` anywhere below - GNU as/LLVM's integrated
# assembler use `@` on most targets but ARM/AArch64 conventionally want `%`
# instead (`@` is that target's comment leader) - three independently
# converging sources in research/incbin_embedding_research.md section 1.
# Omitting `.type` entirely sidesteps the question: it's pure ELF-symbol-table
# metadata, load-bearing for nothing here (only ever take these symbols'
# addresses). `.balign` (not the ARM-power-of-two-ambiguous `.align`) and an
# explicit `"a"` section flag (custom section names don't get free defaults
# the way exactly `.rodata` does) for the same "verified, not guessed" reason.
file(WRITE "${KONATIVE_OUT_FILE}" "\
/* Auto-generated by KonativeGenerateIncbinAsm.cmake - do not hand-edit. */
    .section .rodata.konative.${KONATIVE_BLOB_SYMBOL},\"a\"
    .balign 16
    .global ${KONATIVE_BLOB_SYMBOL}_start
${KONATIVE_BLOB_SYMBOL}_start:
    .incbin \"${KONATIVE_BLOB_PATH}\"
    .global ${KONATIVE_BLOB_SYMBOL}_end
${KONATIVE_BLOB_SYMBOL}_end:

    .balign 8
    .global ${KONATIVE_BLOB_SYMBOL}_size
${KONATIVE_BLOB_SYMBOL}_size:
    .quad ${KONATIVE_BLOB_SYMBOL}_end - ${KONATIVE_BLOB_SYMBOL}_start

    /* Hand-written .S objects skip the compiler's automatic PT_GNU_STACK
       marking - omitting this can make some lld configurations warn/error
       \"missing .note.GNU-stack section implies executable stack\". */
    .section .note.GNU-stack,\"\"
")
```

**Usage** (mirrors `GameHub`'s `gamehub_jvm_dex_blob()` call shape from `JvmDex.cmake`):

```cmake
include(cmake/modules/KonativeEmbedBlob.cmake)

add_library(app_native SHARED main.cpp jni_bridge.cpp)
konative_embed_binary_blob(app_native
  BLOB "${CMAKE_CURRENT_BINARY_DIR}/jvm-dex-app_kt/dexout/classes.dex"
  SYMBOL konative_app_dex)
```

```cpp
extern "C" {
extern const unsigned char konative_app_dex_start[];
extern const unsigned char konative_app_dex_end[];
extern const uint64_t      konative_app_dex_size;
}
// env->NewDirectByteBuffer(const_cast<unsigned char*>(konative_app_dex_start),
//                          static_cast<jlong>(konative_app_dex_size));
```

---

## Sources

**GAS directives / ABI gotchas**: [Sourceware binutils `Type` docs](https://sourceware.org/binutils/docs/as/Type.html) ·
[ARM Developer blog — useful GNU assembler directives](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/useful-assembler-directives-and-macros-for-the-gnu-assembler) ·
[`graphitemaster/incbin`](https://github.com/graphitemaster/incbin) ·
[devever.net — incbin](https://www.devever.net/~hl/incbin) ·
[Laurence Tratt — portable binary blob embedding](https://tratt.net/laurie/blog/2022/whats_the_most_portable_way_to_include_binary_blobs_in_an_executable.html) ·
[llvm/llvm-project#57009 — missing `.note.GNU-stack`](https://github.com/llvm/llvm-project/issues/57009) ·
[Red Hat — linker warnings about executable stacks](https://www.redhat.com/en/blog/linkers-warnings-about-executable-stacks-and-segments).

**CMake build-graph wiring**: [`add_custom_command` docs](https://cmake.org/cmake/help/latest/command/add_custom_command.html) ·
[`OBJECT_DEPENDS` property docs](https://cmake.org/cmake/help/latest/prop_sf/OBJECT_DEPENDS.html) ·
[`enable_language`](https://cmake.org/cmake/help/latest/command/enable_language.html).

**NDK ASM/toolchain mechanics**: [NDK `android.toolchain.cmake` (live source, verified directly, no ASM setup)](https://android.googlesource.com/platform/ndk/+/refs/heads/main/build/cmake/android.toolchain.cmake) ·
[Android NDK CMake guide](https://developer.android.com/ndk/guides/cmake) ·
[android/ndk#1623 — ASM broken with legacy toolchain + CMake 3.19-3.22.1](https://github.com/android/ndk/issues/1623) ·
[android/ndk#234 — historical CMAKE_ASM_COMPILER gap, fixed NDK r14](https://github.com/android/ndk/issues/234) ·
[NDK r23 changelog](https://github.com/android/ndk/wiki/Changelog-r23) ·
[NDK r24 changelog](https://github.com/android/ndk/wiki/Changelog-r24).

**Real-world precedent**: [`official-stockfish/Stockfish`](https://github.com/official-stockfish/Stockfish) ·
[Stockfish Discussion #4703 — incbin path failure under Android Studio CMake](https://github.com/official-stockfish/Stockfish/discussions/4703) ·
[Stockfish Discussion #4873 — building for x86-64 Android](https://github.com/official-stockfish/Stockfish/discussions/4873) ·
[Stockfish PR #3901 — Android NDK cross-compile Makefile fixes](https://github.com/official-stockfish/Stockfish/pull/3901).

**objcopy comparison**: [LLVM `llvm-objcopy` command guide](https://llvm.org/docs/CommandGuide/llvm-objcopy.html) ·
[NDK clang prebuilts tree containing `llvm-objcopy`](https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+/refs/heads/android10-release/clang-r349610/bin/llvm-objcopy) ·
[LLVM Phabricator D50343 — `-I binary -B <arch>` support](https://reviews.llvm.org/D50343).

**Local codebase**: `GameHub/cmake/scripts/bin_to_c_array.py` · `GameHub/cmake/modules/JvmDex.cmake` ·
`Konative/research/research.md` (sections 1–3, 3.6 specifically).
