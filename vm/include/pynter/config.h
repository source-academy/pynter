#ifndef PYNTER_CONFIG_H
#define PYNTER_CONFIG_H

#include "../pynter_config.h"

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Pynter only works on little endian systems
#endif

#ifndef PYNTER_DEBUG_LOGLEVEL
#define PYNTER_DEBUG_LOGLEVEL 0
#endif

#ifdef PYNTER_STATIC_HEAP
#ifndef PYNTER_HEAP_SIZE
// 3.6 MiB (3774874 bytes) -- comfortably under the 4 MiB hard ceiling that
// NaN-boxing's 22-bit heap-pointer field allows (a pointer is encoded as a
// byte offset from the heap's base, see nanbox.h's own doc comment and
// SIHEAP_PTRTONANBOX in heap.h), while leaving headroom below that ceiling.
// See the README's "Memory configuration" section for the full rationale;
// override with -DPYNTER_HEAP_SIZE=<bytes> (CMake) or in pynter_config.h
// (non-CMake build systems, e.g. Arduino) for memory-constrained targets.
#define PYNTER_HEAP_SIZE 0x39999a
#endif
#else
#ifdef PYNTER_HEAP_SIZE
#undef PYNTER_HEAP_SIZE
#endif
#define PYNTER_HEAP_SIZE (siheap_size)
#endif

#ifndef PYNTER_STACK_ENTRIES
#define PYNTER_STACK_ENTRIES 0x200
#endif

#ifndef PYNTER_INLINE
#define PYNTER_INLINE inline
#endif

#ifdef __cplusplus
#define _Static_assert static_assert
#define _Noreturn [[noreturn]]
#define _Bool bool
#define PYNTER_INLINEIFC
#else
#define PYNTER_INLINEIFC PYNTER_INLINE
#endif

#if defined(PYNTER_DEBUG) && defined(NDEBUG)
#undef NDEBUG
#endif

#if defined(PYNTER_DEBUG_MEMORY_CHECK) && defined(NDEBUG)
#warning PYNTER_DEBUG_MEMORY_CHECK has no effect if NDEBUG is set
#endif

#endif
