/**
 * This file is here to provide an exported symbol for all functions
 * declared as inline, which is most functions.
 *
 * We do this by defining away PYNTER_INLINE (which is defined as just
 * `inline` in inline.h), so that the function definitions become normal
 * non-qualified definitions, which make them have external linkage.
 */

#include <pynter/config.h>

#ifdef PYNTER_INLINE
#undef PYNTER_INLINE
#endif
#define PYNTER_INLINE

#include <pynter/heap.h>
#include <pynter/heap_obj.h>
#include <pynter/nanbox.h>
#include <pynter/stack.h>
#include <pynter/vm.h>
#include <pynter/display.h>
