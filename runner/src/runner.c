#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include <pynter/nanbox.h>
#include <pynter/display.h>
#include <pynter.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

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
  "uninitialised heap",
  "stopped",
  "value error",
  "index error"
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
  "function",
  "complex"
};

void setup_internals(void);
void display_object_result(pynter_value_t *res, _Bool is_error);

ssize_t check_posix(ssize_t result, const char *msg) {
  if (result == -1 && errno) {
    perror(msg);
    _exit(1);
  }

  return result;
}

static void print_string(const char *s, bool is_error) {
  (void) is_error;
  printf("%s", s);
}

static void print_integer(int32_t v, bool is_error) {
  (void) is_error;
  printf("%d", v);
}

static void print_float(float v, bool is_error) {
  (void) is_error;
  printf("%f", v);
}

static void print_flush(bool is_error) {
  (void) is_error;
  printf("\n");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    eprintf("Usage: %s <program>\n", argv[0]);
    return 1;
  }

  int program_fd = check_posix(open(argv[1], O_RDONLY), "Failed to open program");
  off_t size;
  {
    struct stat stat_buf;
    check_posix(fstat(program_fd, &stat_buf), "fstat failed");
    size = stat_buf.st_size;
  }
  const unsigned char *program = mmap(NULL, size, PROT_READ, MAP_SHARED, program_fd, 0);
  if (program == MAP_FAILED) {
    check_posix(-1, "mmap failed");
  }

  pynter_printer_float = print_float;
  pynter_printer_string = print_string;
  pynter_printer_integer = print_integer;
  pynter_printer_flush = print_flush;

  setup_internals();

  pynter_value_t result = { 0 };
  pynter_fault_t fault = pynter_run(program, size, &result);

  printf("Program exited with fault %s and result type %s: ",
    fault >= (sizeof(fault_names)/sizeof(fault_names[0])) ? "(unknown fault)" : fault_names[fault],
    result.type >= (sizeof(type_names)/sizeof(type_names[0])) ? "(unknown type)" : type_names[result.type]);

  switch (result.type) {
  case pynter_type_undefined:
    printf("undefined");
    break;
  case pynter_type_none:
    // Route through sidisplay_nanbox rather than hardcoding null/true/false
    // here: Pynter is Python-only and prints None/True/False (see display.h),
    // and these were the two call sites in the runner CLI that bypassed that
    // shared formatting (mirrors the same fix in devices/wasm/wasm/lib.c).
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
  case pynter_type_complex:
    display_object_result(&result, false);
    break;
  default:
    printf("(unable to print value)");
    break;
  }

  printf("\n");

  return 0;
}
