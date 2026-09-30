#ifndef PYNTER_RUNNER_SLING_DISPLAY_H
#define PYNTER_RUNNER_SLING_DISPLAY_H

/*
 * Wire format for the "display" IPC channel that `sling` (the MQTT transport
 * layer that spawns this binary as `<binary> --from-sling <program_path>`,
 * see sling/linux/src/main.c's begin_run_program) uses to relay a running
 * program's printed output to the browser frontend.
 *
 * This is a vendored copy of the struct/enum definitions in the `sling`
 * repo's common/sling_message.h, which sling's own Source-language host
 * program (sinter_host.c) already uses successfully for exactly this
 * purpose. It is copied here rather than included via a relative path into
 * a sibling checkout of `sling` because pynter's own EV3 build
 * (devices/ev3/build.sh / .docker_build.sh) is fully self-contained: its
 * Docker container only bind-mounts the pynter repo itself, so a
 * `../../../sling/...` include would not resolve inside that build.
 *
 * If sling/common/sling_message.h ever changes its wire format, this file
 * must be updated to match by hand.
 */

#include <stdbool.h>
#include <stdint.h>

/* Matches sling/linux/src/common.h's IPC_FD: the fd number that sling's
 * parent process dup2()s one end of a SOCK_DGRAM socketpair onto before
 * execl()ing this binary, when run as `--from-sling`. */
#define SLING_IPC_FD 998

enum sling_message_display_type {
  sling_message_display_type_output = 0,
  sling_message_display_type_error = 1,
  sling_message_display_type_result = 2,
  sling_message_display_type_prompt_response = 4,
  sling_message_display_type_flush = 100,
  sling_message_display_type_self_flushing = 0x100
};

struct __attribute__((packed)) sling_message_display_flush {
  uint32_t message_counter;
  uint16_t message_type;
  uint32_t starting_id;
};
_Static_assert(sizeof(struct sling_message_display_flush) == 10,
               "Wrong sling_message_display_flush size");

struct __attribute__((packed)) sling_message_display {
  uint32_t message_counter;
  uint16_t display_type;
  uint16_t data_type;
  union {
    bool boolean;
    int32_t int32;
    float float32;
    // Excluding null terminator
    uint32_t string_length;
  };
  char string[];
};
_Static_assert(sizeof(struct sling_message_display) == 12, "Wrong sling_message_display size");

#endif
