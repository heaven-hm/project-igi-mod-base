#pragma once

#if defined(_MSC_VER)
#define NATIVE_DECL __declspec(noinline) inline
#elif defined(__GNUC__) || defined(__clang__)
#define NATIVE_DECL __attribute__((noinline)) inline
#else
#define NATIVE_DECL
#endif
