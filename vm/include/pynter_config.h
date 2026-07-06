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
 * Set the size of the statically-allocated heap, in bytes. Ignored if PYNTER_STATIC_HEAP
 * is not set.
 *
 * Defaults to 0x10000.
 */
// #define PYNTER_HEAP_SIZE 0x10000

/**
 * Set the number of entries of the statically-allocated stack, in entries.
 * Each entry is 4 bytes.
 *
 * Defaults to 0x200.
 */
// #define PYNTER_STACK_ENTRIES 0x200

#endif
