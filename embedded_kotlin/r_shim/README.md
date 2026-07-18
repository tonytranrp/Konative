# embedded_kotlin/r_shim/

Hand-written stand-ins for real Android resource-ID classes (`R$id`/`R$string`), needed only
because this project's Compose-to-dex pipeline is currently a hand-run `kotlinc`+`d8`/`r8`
scratchpad recipe with **no AAPT2 resource-linking step at all** (see the parent folder's
`README.md` and `ARCHITECTURE.md` section 6.6's status table). This is **not application code** —
none of it is part of `KonativeEntryPoint`'s actual behavior.

## Why this exists

Several AndroidX libraries (`androidx.lifecycle`, `androidx.savedstate`, `androidx.compose.ui`,
`androidx.core`, `androidx.customview.poolingcontainer`) use `View.setTag(int key, Object value)`/
`getTag(int key)` to attach owner objects to a View tree — `setViewTreeLifecycleOwner()` and
friends work this way. The `int key` in each case is normally `R.id.some_generated_name`, a
constant AAPT2 assigns per-final-app at real resource-link time (the library's own shipped `R.txt`
only has a `0x0` placeholder — confirmed by inspecting the real AARs). A hand-rolled `kotlinc`+`d8`
pipeline with no AAPT2 step has no way to generate these classes, so `r8` reports them as
`Missing class ... R$id` when it tries to shrink/verify the real dependency jars.

**The exact field names in every file here were determined by decompiling the real AndroidX
library bytecode** (`javap` against the actual fetched jars) to find precisely which fields each
missing class needs — not guessed. The exact numeric values were NOT taken from any real app (the
placeholder `0x0` in each library's own `R.txt` means there's no "real" value to copy) — each value
here only needs to be a self-consistent, framework-tag-range integer that doesn't collide with
anything else this module defines, which is all `View.setTag(int, Object)` actually requires.

## The real fix, for whoever builds the actual CMake automation

This is a stopgap, not a design. The real `konative_embed_kotlin_dex()`-style pipeline
(`ARCHITECTURE.md` section 6.6) needs an actual AAPT2 resource-linking step (even a minimal one,
against a synthetic empty app just to link the AndroidX libraries' own resources) to produce real
`R.class` files with library-consistent values, the same way any Gradle/AGP build does automatically
via `processDebugResources`. Once that exists, delete this entire folder.

## Adding to this folder

Only add a new file here if `r8`'s shrink/verify pass reports another genuinely missing `R$id`/
`R$string` class from a real AndroidX dependency — determine the exact fields the same way (decompile
the actual referencing method R8 names, don't guess), and add this same explanatory comment.
