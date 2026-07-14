#ifndef PYNTER_H
#define PYNTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  pynter_type_undefined = 1,
  pynter_type_none = 2,
  pynter_type_boolean = 3,
  pynter_type_integer = 4,
  pynter_type_float = 5,
  pynter_type_string = 6,
  pynter_type_array = 7,
  pynter_type_function = 8
} pynter_type_t;

typedef enum {
  pynter_fault_none = 0,
  pynter_fault_out_of_memory = 1,
  pynter_fault_type = 2,
  pynter_fault_divide_by_zero = 3,
  pynter_fault_stack_overflow = 4,
  pynter_fault_stack_underflow = 5,
  pynter_fault_uninitialised_load = 6,
  pynter_fault_invalid_load = 7,
  pynter_fault_invalid_program = 8,
  pynter_fault_internal_error = 9,
  pynter_fault_function_arity = 10,
  pynter_fault_program_error = 11,
  pynter_fault_uninitialised_heap = 12,
  pynter_fault_stopped = 13
} pynter_fault_t;

typedef struct {
  pynter_type_t type;
  union {
    bool boolean_value;
    int32_t integer_value;
    float float_value;
    const char *string_value;
    uint32_t object_value;
  };
} pynter_value_t;

/**
 * Runs a program.
 *
 * This is the main entrypoint for programs using the Pynter VM as a library.
 */
pynter_fault_t pynter_run(const unsigned char *code, const size_t code_size, pynter_value_t *result);

/**
 * Set up the heap.
 *
 * This function is a no-op if PYNTER_STATIC_HEAP is defined.
 */
void pynter_setup_heap(void *heap, size_t size);

/**
 * Stops the currently running program.
 *
 * This function stops the currently running program.
*/

void pynter_stop(void);

/**
 * The type of a string printer function.
 *
 * A newline should not be appended by the function.
 */
typedef void (*pynter_printfn_string)(const char *str, bool is_error);

/**
 * The type of an integer printer function.
 *
 * A newline should not be appended by the function.
 */
typedef void (*pynter_printfn_integer)(int32_t value, bool is_error);

/**
 * The type of a float printer function.
 *
 * A newline should not be appended by the function.
 */
typedef void (*pynter_printfn_float)(float value, bool is_error);

/**
 * The type of a printer flush function.
 *
 * This function should flush any prints buffered from previous calls to the
 * printer functions. If printing to the console, this function might just
 * print a newline, for example.
 */
typedef void (*pynter_printfn_flush)(bool is_error);

extern pynter_printfn_string pynter_printer_string;
extern pynter_printfn_integer pynter_printer_integer;
extern pynter_printfn_float pynter_printer_float;
extern pynter_printfn_flush pynter_printer_flush;

#ifdef __cplusplus
}
#endif

#endif
