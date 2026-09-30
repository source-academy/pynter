#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <pynter/nanbox.h>
#include <pynter/display.h>
#include <pynter.h>

#include "sling_display.h"

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

// Set once at startup: true iff we were invoked as `<binary> --from-sling <program>`,
// i.e. we are a child process spawned by `sling` (see the comment on `main` below)
// rather than a plain interactive CLI invocation.
static bool from_sling = false;

// Buffers printed output between pynter_printer_* calls so that consecutive
// print() fragments (e.g. the separate string/int/float pieces of a single
// Python print(a, b, c) call) are coalesced into as few "display" IPC
// messages as possible, matching sling's sinter_host.c host program (see
// sling/linux/src/sinter_host.c's printf_buf/print_flush) so the browser
// frontend - which already parses sinter_host's messages - handles ours
// identically.
static char display_buf[0x1000];
static size_t display_buf_index = 0;
static bool display_buf_fragmented = false;

static inline enum sling_message_display_type print_type(bool is_error) {
  return is_error ? sling_message_display_type_error : sling_message_display_type_output;
}

// Sends a raw, already-framed message to sling's parent process over the
// IPC socket it set up before exec()ing us (see sling/linux/src/main.c's
// begin_run_program: it dup2()s one end of a SOCK_DGRAM socketpair onto fd
// SLING_IPC_FD before execl()ing this binary with --from-sling). This is a
// connected datagram socket, not a plain pipe/stdout, so each call to
// send_ipc_raw must correspond to exactly one complete sling_message_display
// (or sling_message_display_flush) struct.
static void send_ipc_raw(const void *data, size_t size) {
  ssize_t sendres = send(SLING_IPC_FD, data, size, 0);
  if (sendres == -1) {
    _exit(1);
  }
}

// pynter_type_t (pynter.h) deliberately reuses the same numeric codes as
// sinter's sinter_type_t (sling/deps/sinter/vm/include/sinter.h) for the
// types they share (undefined/boolean/integer/float/string/array/function),
// so pynter_type_integer/_float/_string can be written directly into a
// sling_message_display's data_type field with no translation, exactly as
// sinter_host.c does with its own sinter_type_t values.
static void send_display_string(const char *str, uint16_t type) {
  size_t len = strlen(str);
  size_t message_len = sizeof(struct sling_message_display) + len + 1;
  struct sling_message_display *msg = calloc(1, message_len);
  if (!msg) {
    _exit(1);
  }
  msg->display_type = type;
  msg->data_type = pynter_type_string;
  msg->string_length = (uint32_t) len;
  memcpy(msg->string, str, len + 1);
  send_ipc_raw(msg, message_len);
  free(msg);
}

static void send_display_integer(int32_t v, uint16_t type) {
  struct sling_message_display *msg = calloc(1, sizeof(*msg));
  if (!msg) {
    _exit(1);
  }
  msg->display_type = type;
  msg->data_type = pynter_type_integer;
  msg->int32 = v;
  send_ipc_raw(msg, sizeof(*msg));
  free(msg);
}

static void send_display_float(float v, uint16_t type) {
  struct sling_message_display *msg = calloc(1, sizeof(*msg));
  if (!msg) {
    _exit(1);
  }
  msg->display_type = type;
  msg->data_type = pynter_type_float;
  msg->float32 = v;
  send_ipc_raw(msg, sizeof(*msg));
  free(msg);
}

// Appends formatted text to display_buf, flushing (sending) the buffer as an
// intermediate fragment if it doesn't fit. Returns false if even a freshly
// emptied buffer can't hold this piece of text, in which case the caller
// must send it as its own message instead. Mirrors sinter_host.c's
// printf_buf exactly, including the fragmented-flush protocol that
// print_flush()/main.c's relay loop rely on.
__attribute__((format(printf, 2, 3))) static bool printf_buf(bool is_error, const char *format,
                                                              ...) {
  va_list args;
  va_start(args, format);
  const size_t can_write = sizeof(display_buf) - display_buf_index;
  int written = vsnprintf(display_buf + display_buf_index, can_write, format, args);
  va_end(args);

  if (written < 0) {
    // Formatting error - nothing sensible to buffer.
    return true;
  }

  if ((size_t) written < can_write) {
    display_buf_index += (size_t) written;
    return true;
  }

  // Doesn't fit: send what's buffered so far as a fragment, then retry into
  // a freshly emptied buffer.
  display_buf_fragmented = true;
  display_buf[display_buf_index] = '\0';
  send_display_string(display_buf, print_type(is_error));
  display_buf_index = 0;

  if ((size_t) written >= sizeof(display_buf)) {
    // Won't fit even in an empty buffer - give up buffering it.
    return false;
  }

  va_start(args, format);
  written = vsnprintf(display_buf, sizeof(display_buf), format, args);
  va_end(args);
  display_buf_index += (size_t) written;
  return true;
}

static void print_string(const char *s, bool is_error) {
  if (!from_sling) {
    printf("%s", s);
    return;
  }
  if (!printf_buf(is_error, "%s", s)) {
    send_display_string(s, print_type(is_error));
  }
}

static void print_integer(int32_t v, bool is_error) {
  if (!from_sling) {
    printf("%d", v);
    return;
  }
  if (!printf_buf(is_error, "%" PRId32, v)) {
    send_display_integer(v, print_type(is_error));
  }
}

static void print_float(float v, bool is_error) {
  if (!from_sling) {
    printf("%f", v);
    return;
  }
  if (!printf_buf(is_error, "%f", v)) {
    send_display_float(v, print_type(is_error));
  }
}

static void print_flush(bool is_error) {
  if (!from_sling) {
    printf("\n");
    return;
  }

  if (display_buf_fragmented) {
    struct sling_message_display_flush flush_message = {
      .message_type = sling_message_display_type_flush
    };
    send_ipc_raw(&flush_message, sizeof(flush_message));
    display_buf_fragmented = false;
    display_buf_index = 0;
    return;
  }

  send_display_string(display_buf,
                       print_type(is_error) | sling_message_display_type_self_flushing);
  display_buf_index = 0;
}

int main(int argc, char *argv[]) {
  // `sling` (the transport layer shared with the Source pipeline's sinter_host) always invokes
  // whatever binary SINTER_HOST_PATH points at as `<binary> --from-sling <program_path>` (see
  // sling/linux/src/main.c's begin_run_program). sinter_host parses that flag; this runner never
  // did, so when SINTER_HOST_PATH points here directly, argv[1] was literally the string
  // "--from-sling" and the real path in argv[2] was never read - producing "Failed to open
  // program: No such file or directory" on every single run, regardless of the program itself.
  int path_arg = 1;
  if (argc > 1 && strcmp(argv[1], "--from-sling") == 0) {
    path_arg = 2;
    from_sling = true;
  }
  if (argc <= path_arg) {
    eprintf("Usage: %s [--from-sling] <program>\n", argv[0]);
    return 1;
  }

  int program_fd = check_posix(open(argv[path_arg], O_RDONLY), "Failed to open program");
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
