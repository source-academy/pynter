#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

// Backs the pointer siwasm_run() returns: JS-side callers (see py-slang's
// pynter-wasm.ts) read the result directly out of WASM memory as an 8-byte
// {type: u32, value: 4 bytes} struct, which is exactly pynter_value_t's
// layout on wasm32. Static (not stack-local) so the pointer stays valid
// after this function returns; zeroed on every call so a faulting run
// deterministically reports type 0 ("unknown"/no result) rather than
// leftover data from a previous run.
static pynter_value_t wasm_result;

EMSCRIPTEN_KEEPALIVE
pynter_value_t *siwasm_run(unsigned char *code, size_t code_size) {
  pynter_printer_float = print_float;
  pynter_printer_integer = print_integer;
  pynter_printer_string = print_string;
  pynter_printer_flush = print_flush;

  memset(&wasm_result, 0, sizeof(wasm_result));
  pynter_fault_t fault = pynter_run(code, code_size, &wasm_result);

  if (fault) {
    printf("Program exited unsuccessfully: %s\n",
      fault >= (sizeof(fault_names)/sizeof(fault_names[0])) ? "(unknown fault)" : fault_names[fault]);
    memset(&wasm_result, 0, sizeof(wasm_result));
    return &wasm_result;
  }

  printf("Program exited with result type %s: ",
    wasm_result.type >= (sizeof(type_names)/sizeof(type_names[0])) ? "(unknown type)" : type_names[wasm_result.type]);

  switch (wasm_result.type) {
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
    sidisplay_nanbox(NANBOX_OFBOOL(wasm_result.boolean_value), false);
    break;
  case pynter_type_integer:
    printf("%d", wasm_result.integer_value);
    break;
  case pynter_type_float:
    printf("%f", wasm_result.float_value);
    break;
  case pynter_type_string:
    printf("%s", wasm_result.string_value);
    break;
  case pynter_type_array:
  case pynter_type_function:
    display_object_result(&wasm_result, false);
    break;
  default:
    printf("(unable to print value)");
    break;
  }

  printf("\n");

  return &wasm_result;
}
