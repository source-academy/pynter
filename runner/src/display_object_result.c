#include <pynter/vm.h>
#include <pynter/program.h>
#include <pynter/heap_obj.h>
#include <pynter/display.h>
#include <pynter.h>

void display_object_result(pynter_value_t *res, _Bool is_error) {
  if (res->type == pynter_type_array || res->type == pynter_type_function || res->type == pynter_type_complex) {
    sinanbox_t arr = NANBOX_WITH_I32(res->object_value);
    sidisplay_nanbox(arr, is_error);
  }
}
