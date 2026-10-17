#ifndef PYNTER_LAYOUT_H
#define PYNTER_LAYOUT_H

/**
 * Selects the value layout.
 *
 * By default Pynter uses 64-bit NaN-boxing: a value is a 64-bit word that is
 * either a double-precision float or a NaN carrying a 32-bit payload (small
 * integer or heap offset). This gives Python-sized floats (f64), 32-bit small
 * ints and heap offsets of 32 bits, so the heap is no longer limited to 4 MiB.
 * See nanbox.h.
 *
 * Define PYNTER_NANBOX32 to use the compact 32-bit layout instead: single
 * precision floats, 21-bit small ints and 22-bit heap offsets (at most 4 MiB
 * of heap). It is meant for targets with a few KiB of RAM, such as AVR
 * Arduino, where a value must fit in 4 bytes. It is selected automatically on
 * AVR.
 */

// (pynter_config.h can define PYNTER_NANBOX32, for build systems without defines)
#include "../pynter_config.h"

#if !defined(PYNTER_NANBOX32) && defined(__AVR__)
#define PYNTER_NANBOX32
#endif

#include <float.h>
#include <inttypes.h>
#include <stdint.h>

#ifdef PYNTER_NANBOX32

/** The float type of Python floats and of the components of complex numbers. */
typedef float sifloat_t;
/**
 * The type of the bits of a NaN-box, used to pass a NaN-box through an
 * interface that is oblivious to the layout (see pynter_value_t).
 */
typedef uint32_t sinanbox_bits_t;
/** Appends the suffix of the single-precision libm variant: SIFL(sqrt) is sqrtf. */
#define SIFL(fn) fn##f
/** printf format of an sinanbox_bits_t in hexadecimal. */
#define SINANBOX_BITS_FMT "%08" PRIx32

#else

#if DBL_MANT_DIG != 53
#error 64-bit NaN-boxing needs a 64-bit double; define PYNTER_NANBOX32 for this target
#endif

typedef double sifloat_t;
typedef uint64_t sinanbox_bits_t;
/** Appends the suffix of the double-precision libm variant: SIFL(sqrt) is sqrt. */
#define SIFL(fn) fn
#define SINANBOX_BITS_FMT "%016" PRIx64

#endif

#endif
