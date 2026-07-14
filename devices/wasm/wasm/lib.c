#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include <emscripten.h>

#include <pynter/nanbox.h>
#include <pynter/display.h>
#include <pynter.h>

static char *heap = NULL;

static const char *fault_names[] = {
  "no fault",
  "out of memory",
  "type error",
  "divide by zero",
  "stack overflow",
  "stack underflow",
  "uninitialised load",
  "invalid load",
  "invalid program",
  "internal error",
  "incorrect function arity",
  "program called error()",
  "uninitialised heap"
};

static const char *type_names[] = {
  "unknown",
  "undefined",
  "none",
  "boolean",
  "integer",
  "float",
  "string",
  "array",
  "function"
};

void display_object_result(pynter_value_t *res, _Bool is_error) {
  if (res->type == pynter_type_array || res->type == pynter_type_function) {
    sinanbox_t arr = NANBOX_WITH_I32(res->object_value);
    sidisplay_nanbox(arr, is_error);
  }
}

static void print_string(const char *s, bool is_error) {
  fprintf(is_error ? stderr : stdout, "%s", s);
}

static void print_integer(int32_t v, bool is_error) {
  fprintf(is_error ? stderr : stdout, "%d", v);
}

static void print_float(float v, bool is_error) {
  fprintf(is_error ? stderr : stdout, "%f", v);
}

static void print_flush(bool is_error) {
  fprintf(is_error ? stderr : stdout, "\n");
}

EMSCRIPTEN_KEEPALIVE
void siwasm_alloc_heap(size_t size) {
  if (heap) {
    free(heap);
  }

  heap = malloc(size);
  if (!heap) {
    fprintf(stderr, "Warning: failed to allocate heap of size %ld; try again\n", size);
    pynter_setup_heap(0, 0);
    return;
  }
  pynter_setup_heap(heap, size);
}

EMSCRIPTEN_KEEPALIVE
void *siwasm_alloc(size_t size) {
  return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void siwasm_free(void *ptr) {
  free(ptr);
}

EMSCRIPTEN_KEEPALIVE
void siwasm_run(unsigned char *code, size_t code_size) {
  pynter_printer_float = print_float;
  pynter_printer_integer = print_integer;
  pynter_printer_string = print_string;
  pynter_printer_flush = print_flush;

  pynter_value_t result;
  pynter_fault_t fault = (uint8_t) pynter_run(code, code_size, &result);

  if (fault) {
    printf("Program exited unsuccessfully: %s\n",
      fault >= (sizeof(fault_names)/sizeof(fault_names[0])) ? "(unknown fault)" : fault_names[fault]);
    return;
  }

  printf("Program exited with result type %s: ",
    result.type >= (sizeof(type_names)/sizeof(type_names[0])) ? "(unknown type)" : type_names[result.type]);

  switch (result.type) {
  case pynter_type_undefined:
    printf("undefined");
    break;
  case pynter_type_none:
    // Route through sidisplay_nanbox rather than hardcoding null/true/false
    // here: Pynter is Python-only and prints None/True/False (see display.h),
    // and these were the two call sites that bypassed that shared formatting.
    sidisplay_nanbox(NANBOX_OFNULL(), false);
    break;
  case pynter_type_boolean:
    sidisplay_nanbox(NANBOX_OFBOOL(result.boolean_value), false);
    break;
  case pynter_type_integer:
    printf("%d", result.integer_value);
    break;
  case pynter_type_float:
    printf("%f", result.float_value);
    break;
  case pynter_type_string:
    printf("%s", result.string_value);
    break;
  case pynter_type_array:
  case pynter_type_function:
    display_object_result(&result, false);
    break;
  default:
    printf("(unable to print value)");
    break;
  }

  printf("\n");
}
