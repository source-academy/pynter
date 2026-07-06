#include <pynter/config.h>

#ifdef PYNTER_DEBUG_ABORT_ON_FAULT
#include <assert.h>
#endif

#include <pynter/fault.h>
#include <pynter/vm.h>

jmp_buf pynter_fault_jmp = { 0 };

/**
 * Faults with the given reason. This will immediately abort execution.
 */
_Noreturn void sifault(pynter_fault_t reason) {
  sistate.fault_reason = reason;
#ifdef PYNTER_DEBUG_ABORT_ON_FAULT
  assert("Faulting." == 0);
#endif
  longjmp(pynter_fault_jmp, 1);
}
