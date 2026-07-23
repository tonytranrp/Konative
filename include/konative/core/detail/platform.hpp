#pragma once

// Compiler/platform detection macros shared across every module's detail/ layer. Not part of the
// public API - see ARCHITECTURE.md section 2 for the detail/ convention (Boost/GLM/Hana all follow this
// "namespace detail + folder detail/" split for the implementation a template can't hide behind a
// .cpp boundary).

#if defined(__ANDROID__)
#define KONATIVE_PLATFORM_ANDROID 1
#else
#define KONATIVE_PLATFORM_ANDROID 0
#endif

#if defined(__clang__)
#define KONATIVE_COMPILER_CLANG 1
#else
#define KONATIVE_COMPILER_CLANG 0
#endif
