// Exactly one translation unit in the whole tests/ target must define this before including
// doctest.h - it's what generates doctest's actual runtime implementation (Result/
// ExpressionDecomposer/etc.) and a main() function. Every other test_*.cpp just #includes
// doctest.h plain and defines TEST_CASEs - see tests/README.md.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
