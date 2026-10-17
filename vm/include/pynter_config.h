#ifndef PYNTER_PUBLIC_CONFIG_H
#define PYNTER_PUBLIC_CONFIG_H

/**
 * Pynter configuration header.
 *
 * Note: if you are using CMake, it is better if you set the configuration
 * using CMake properties instead.
 *
 * This is meant for use when your build system does not facilitate setting
 * defines (e.g. Arduino).
 */

/**
 * Set the debug log level.
 *
 * - 0: all debug output is disabled; recommended for release/production
 * - 1: prints reasons for most faults/errors
 * - 2: traces every instruction executed and every push on/pop off the stack
 *
 * Defaults to 0.
 */
// #define PYNTER_DEBUG_LOGLEVEL 0

/**
 * Enable the statically-allocated heap.
 *
 * Otherwise, the heap must be allocated by the host program and passed to
 * Pynter using pynter_setup_heap.
 *
 * Off by default.
 */
// #define PYNTER_STATIC_HEAP

/**
 * Set the size of the statically-allocated heap, in bytes, a multiple of 8. Ignored if
 * PYNTER_STATIC_HEAP is not set. With PYNTER_NANBOX32, NaN-boxing's 22-bit heap-pointer
 * field imposes a hard ceiling of 0x400000 (4 MiB) -- see the README's "Memory
 * configuration" section. Lower this on memory-constrained targets (e.g. Arduino/ESP32).
 *
 * Defaults to 0x399998 (3.6 MiB).
 */
// #define PYNTER_HEAP_SIZE 0x399998

/**
 * Use the compact 32-bit NaN-box: single-precision floats, 21-bit small integers and
 * a heap of at most 4 MiB, with values of 4 bytes. Otherwise values are 64-bit NaN-boxes,
 * with double-precision floats, 32-bit small integers and a heap of up to 4 GiB, and
 * values of 8 bytes. See the README's "Value layout" section.
 *
 * Defined automatically on AVR (Arduino). Off elsewhere by default.
 */
// #define PYNTER_NANBOX32

/**
 * Set the number of entries of the statically-allocated stack, in entries.
 * Each entry is 8 bytes (4 bytes with PYNTER_NANBOX32).
 *
 * Defaults to 0x200.
 */
// #define PYNTER_STACK_ENTRIES 0x200

#endif
