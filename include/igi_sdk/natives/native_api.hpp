#pragma once

#if defined(_MSC_VER)
#define NATIVE_DECL __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NATIVE_DECL __attribute__((noinline))
#else
#define NATIVE_DECL
#endif
