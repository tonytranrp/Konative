#pragma once

// Explicit, deliberate symbol-visibility control on the Kotlin/Native <-> C++ boundary
// (ARCHITECTURE.md \xc2\xa76.3's mitigation for the documented libc++/symbol-conflict risk) - never rely
// on compiler default visibility for anything crossing this boundary in either direction.
#if defined(__GNUC__) || defined(__clang__)
#define KONATIVE_ABI_EXPORT __attribute__((visibility("default")))
#else
#define KONATIVE_ABI_EXPORT
#endif
