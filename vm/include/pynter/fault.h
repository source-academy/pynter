#ifndef PYNTER_FAULT_H
#define PYNTER_FAULT_H

#include "config.h"

#include <setjmp.h>

#include <pynter.h>

#ifdef __cplusplus
extern "C" {
#endif

extern jmp_buf pynter_fault_jmp;

/**
 * Halts the VM with the given fault reason (using `longjmp`).
 *
 * Can only be called from a method within [pynter_run](@ref pynter_run).
 */
_Noreturn void sifault(pynter_fault_t);

#define PYNTER_FAULTED() setjmp(pynter_fault_jmp)

#ifdef __cplusplus
}
#endif

#endif
