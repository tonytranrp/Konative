#pragma once

#include "konative/interop/detail/symbol_visibility.hpp"

// KONATIVE_C_ABI_EXPORT: wraps a C++ function for consumption by Kotlin/Native's `cinterop`
// (which binds against plain C headers - see ARCHITECTURE.md section 6.3) - extern "C" linkage, explicit
// default visibility, and (critically) no C++ exception allowed to unwind across it, since an
// unwound exception cannot safely cross a plain-C-ABI call into Kotlin/Native code.
#define KONATIVE_C_ABI_EXPORT extern "C" KONATIVE_ABI_EXPORT
