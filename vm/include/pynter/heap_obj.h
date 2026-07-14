#ifndef PYNTER_HEAP_OBJ_H
#define PYNTER_HEAP_OBJ_H

#include "config.h"
#include "opcode.h"
#include "heap.h"
#include "debug.h"
#include "internal_fn.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
struct siheap_env;
typedef siheap_env siheap_env_t;
#else
typedef struct siheap_env {
  siheap_header_t header;
  struct siheap_env *parent;
  uint16_t entry_count;
  sinanbox_t entry[];
} siheap_env_t;
#endif

#define SIENV_SIZE(entry_count) (sizeof(siheap_env_t) + (entry_count)*sizeof(sinanbox_t))

/**
 * Create a new environment heap object.
 *
 * This increments the reference count on the parent environment, if any.
 */
PYNTER_INLINEIFC siheap_env_t *sienv_new(
  siheap_env_t *parent,
  const uint16_t entry_count);
#ifndef __cplusplus
PYNTER_INLINEIFC siheap_env_t *sienv_new(
  siheap_env_t *parent,
  const uint16_t entry_count) {
  siheap_env_t *env = (siheap_env_t *) siheap_malloc(SIENV_SIZE(entry_count), sitype_env);
  env->parent = parent;
  env->entry_count = entry_count;
  for (size_t i = 0; i < entry_count; ++i) {
    env->entry[i] = NANBOX_OFEMPTY();
  }
  if (parent) {
    siheap_ref(parent);
  }

  return env;
}
#endif

PYNTER_INLINEIFC void sienv_destroy(siheap_env_t *const env);
#ifndef __cplusplus
PYNTER_INLINEIFC void sienv_destroy(siheap_env_t *const env) {
  for (size_t i = 0; i < env->entry_count; ++i) {
    siheap_derefbox(env->entry[i]);
  }
  if (env->parent) {
    siheap_deref(env->parent);
  }
}
#endif

/**
 * Get a value from the environment.
 *
 * Note: the caller is responsible for incrementing the reference count, if needed.
 */
PYNTER_INLINEIFC sinanbox_t sienv_get(siheap_env_t *const env, const uint16_t index);
#ifndef __cplusplus
PYNTER_INLINEIFC sinanbox_t sienv_get(siheap_env_t *const env, const uint16_t index) {
#ifndef PYNTER_DISABLE_CHECKS
  if (index >= env->entry_count) {
    sifault(pynter_fault_invalid_load);
    return NANBOX_OFEMPTY();
  }
#endif

  return env->entry[index];
}
#endif

/**
 * Put a value into the environment.
 *
 * If a heap pointer is replaced, the heap object's reference count is decremented.
 *
 * Note: the caller "passes" its reference to the environment. That is, the caller should increment the reference
 * count of the heap object (if the value is a pointer) if it is going to continue holding on to the value.
 */
PYNTER_INLINEIFC void sienv_put(siheap_env_t *const env, const uint16_t index, const sinanbox_t val);

#ifndef __cplusplus
PYNTER_INLINEIFC void sienv_put(siheap_env_t *const env, const uint16_t index, const sinanbox_t val) {
#ifndef PYNTER_DISABLE_CHECKS
  if (index >= env->entry_count) {
    sifault(pynter_fault_invalid_load);
    return;
  }
#endif

  siheap_derefbox(env->entry[index]);
  env->entry[index] = val;
}
#endif

PYNTER_INLINEIFC siheap_env_t *sienv_getparent(siheap_env_t *env, unsigned int index);
#ifndef __cplusplus
PYNTER_INLINEIFC siheap_env_t *sienv_getparent(siheap_env_t *env, unsigned int index) {
  while (env && index--) {
    env = env->parent;
  }
  return env;
}
#endif

typedef struct {
  siheap_header_t header;
  const pvm_function_t *code;
  siheap_env_t *env;
} siheap_function_t;

PYNTER_INLINE siheap_function_t *sifunction_new(const pvm_function_t *code, siheap_env_t *parent_env) {
  siheap_function_t *fn = (siheap_function_t *) siheap_malloc(sizeof(siheap_function_t), sitype_function);
  fn->code = code;
  fn->env = parent_env;
  siheap_ref(parent_env);

  return fn;
}

PYNTER_INLINE void sifunction_destroy(siheap_function_t *fn) {
  if (fn->env) {
    siheap_deref(fn->env);
  }
}

/**
 * A `range(start, stop, step)` iterator's state — op_new_iter/op_for_iter's
 * only iterable (Python's own `for` loop grammar at this VM's target chapter
 * is validated range()-only upstream in py-slang, so no other iterable shape
 * ever reaches this VM). `current`/`stop`/`step` are plain C ints, not
 * nanboxes: the values *produced* per iteration go through NANBOX_WRAP_INT
 * (falling back to float once out of the 21-bit small-int range), but the
 * iterator's own bookkeeping needs full int32 range to match range()'s own
 * argument range, matching py-slang's PVMLIterator's `current`/`stop`/`step`
 * fields (see src/engines/pvml/types.ts and pvml-interpreter.ts's FOR_ITER
 * case) exactly, just without an array-iteration variant.
 */
typedef struct {
  siheap_header_t header;
  int32_t current;
  int32_t stop;
  int32_t step;
} siheap_iterator_t;

PYNTER_INLINE siheap_iterator_t *siiterator_new(int32_t start, int32_t stop, int32_t step) {
  siheap_iterator_t *iter = (siheap_iterator_t *) siheap_malloc(sizeof(siheap_iterator_t), sitype_iterator);
  iter->current = start;
  iter->stop = stop;
  iter->step = step;

  return iter;
}

/** No owned heap references (range-only, no backing array) — nothing to release. */
PYNTER_INLINE void siiterator_destroy(siheap_iterator_t *iter) {
  (void) iter;
}

typedef struct {
  siheap_header_t header;
  const opcode_t *return_address;
  sinanbox_t *saved_stack_bottom;
  sinanbox_t *saved_stack_limit;
  sinanbox_t *saved_stack_top;
  siheap_env_t *saved_env;
} siheap_frame_t;

PYNTER_INLINE siheap_frame_t *siframe_new(void) {
  return (siheap_frame_t *) siheap_malloc(
    sizeof(siheap_frame_t), sitype_frame);
}

typedef struct {
  siheap_header_t header;
  const pvm_constant_t *string;
} siheap_strconst_t;

PYNTER_INLINE siheap_strconst_t *sistrconst_new(const pvm_constant_t *string) {
  siheap_strconst_t *obj = (siheap_strconst_t *) siheap_malloc(sizeof(siheap_strconst_t), sitype_strconst);
  obj->string = string;

  return obj;
}

typedef struct {
  siheap_header_t header;
  siheap_header_t *left;
  siheap_header_t *right;
} siheap_strpair_t;

/**
 * Creates a new string pair, representing concatenation.
 *
 * The refcount of left and right are incremented.
 */
PYNTER_INLINE siheap_strpair_t *sistrpair_new(siheap_header_t *left, siheap_header_t *right) {
  siheap_strpair_t *obj = (siheap_strpair_t *) siheap_malloc(sizeof(siheap_strpair_t), sitype_strpair);
  obj->left = left;
  obj->right = right;

  siheap_ref(left);
  siheap_ref(right);

  return obj;
}

PYNTER_INLINE void sistrpair_destroy(siheap_strpair_t *obj) {
  siheap_deref(obj->left);
  if (obj->right) {
    siheap_deref(obj->right);
  }
}

#ifdef __cplusplus
struct siheap_string;
typedef struct siheap_string siheap_string_t;
#else
typedef struct siheap_string {
  siheap_header_t header;
  address_t size;
  char string[];
} siheap_string_t;
#endif

PYNTER_INLINEIFC siheap_string_t *sistring_new(address_t size);
#ifndef __cplusplus
PYNTER_INLINEIFC siheap_string_t *sistring_new(address_t size) {
  siheap_string_t *obj = (siheap_string_t *) siheap_malloc(sizeof(siheap_string_t) + size, sitype_string);
  obj->size = size;
  return obj;
}
#endif

siheap_string_t *sistrpair_flatten(siheap_strpair_t *obj);

PYNTER_INLINEIFC const char *sistrobj_tocharptr(siheap_header_t *obj);
#ifndef __cplusplus
PYNTER_INLINEIFC const char *sistrobj_tocharptr(siheap_header_t *obj) {
  switch (obj->type) {
  case sitype_strconst: {
    siheap_strconst_t *v = (siheap_strconst_t *) obj;
    return (const char *) v->string->data;
  }

  case sitype_strpair: {
    siheap_strpair_t *v = (siheap_strpair_t *) obj;
    siheap_string_t *str = sistrpair_flatten(v);
    return str->string;
  }

  case sitype_string: {
    siheap_string_t *v = (siheap_string_t *) obj;
    return v->string;
  }

  case sitype_array:
  case sitype_array_data:
  case sitype_empty:
  case sitype_free:
  case sitype_function:
  case sitype_frame:
  case sitype_env:
  case sitype_intcont:
  case sitype_iterator:
  default:
    SIBUGM("Unknown string type\n");
    sifault(pynter_fault_internal_error);
    break;
  }
}
#endif

PYNTER_INLINE _Bool siheap_is_string(siheap_header_t *h) {
  switch (h->type) {
  case sitype_strconst:
  case sitype_strpair:
    return true;
  case sitype_string:
    SIBUGM("siheap_string_t seen on stack\n");
    sifault(pynter_fault_internal_error);
    return false;
  case sitype_array:
  case sitype_array_data:
  case sitype_empty:
  case sitype_free:
  case sitype_function:
  case sitype_frame:
  case sitype_env:
  case sitype_intcont:
  case sitype_iterator:
  default:
    return false;
  }
}

#ifdef __cplusplus
struct siheap_array_data;
typedef struct siheap_array_data siheap_array_data_t;
#else
typedef struct siheap_array_data {
  siheap_header_t header;
  sinanbox_t data[];
} siheap_array_data_t;
#endif

typedef struct {
  siheap_header_t header;
  address_t alloc_size;
  address_t count;
  siheap_array_data_t *data;
} siheap_array_t;

PYNTER_INLINEIFC siheap_array_t *siarray_new(address_t alloc_size);
PYNTER_INLINEIFC sinanbox_t siarray_get(siheap_array_t *array, address_t index);
PYNTER_INLINEIFC void siarray_put(siheap_array_t *array, address_t index, sinanbox_t v);
PYNTER_INLINEIFC void siarray_destroy(siheap_array_t *array);

#ifndef __cplusplus
PYNTER_INLINEIFC siheap_array_t *siarray_new(address_t alloc_size) {
  siheap_array_t *array = (siheap_array_t *) siheap_malloc(sizeof(siheap_array_t), sitype_array);
  array->count = 0;
  array->alloc_size = alloc_size;
  array->data = (siheap_array_data_t *) siheap_malloc(sizeof(siheap_array_data_t) + alloc_size*sizeof(sinanbox_t), sitype_array_data);

  for (address_t i = 0; i < alloc_size; ++i) {
    array->data->data[i] = NANBOX_OFUNDEF();
  }

  return array;
}

PYNTER_INLINEIFC sinanbox_t siarray_get(siheap_array_t *array, address_t index) {
  if (index >= array->count) {
    return NANBOX_OFUNDEF();
  }

  return array->data->data[index];
}

PYNTER_INLINEIFC void siarray_put(siheap_array_t *array, address_t index, sinanbox_t v) {
  if (index >= array->alloc_size) {
    address_t new_size = array->alloc_size;
    while (new_size && new_size <= index) {
      new_size <<= 1;
    }
    if (!new_size) {
      new_size = UINT32_MAX;
    }
    array->data = (siheap_array_data_t *) siheap_mrealloc(&array->data->header,
      sizeof(siheap_array_data_t) + new_size*sizeof(sinanbox_t));
    for (address_t i = array->alloc_size; i < new_size; ++i) {
      array->data->data[i] = NANBOX_OFUNDEF();
    }
    array->alloc_size = new_size;
  }

  siheap_derefbox(array->data->data[index]);
  array->data->data[index] = v;
  if (array->count <= index) {
    array->count = index + 1;
  }
}

PYNTER_INLINEIFC void siarray_destroy(siheap_array_t *array) {
  for (address_t i = 0; i < array->alloc_size; ++i) {
    siheap_derefbox(array->data->data[i]);
  }
  siheap_deref(array->data);
}
#endif

#ifdef __cplusplus
struct siheap_intcont;
typedef struct siheap_intcont siheap_intcont_t;
#else
typedef struct siheap_intcont {
  siheap_header_t header;
  sivmfnptr_t fn;
  address_t argc;
  sinanbox_t argv[];
} siheap_intcont_t;
#endif

PYNTER_INLINEIFC siheap_intcont_t *siintcont_new(sivmfnptr_t fn, address_t argc);
PYNTER_INLINEIFC void siintcont_destroy(siheap_intcont_t *ic);

#ifndef __cplusplus
PYNTER_INLINEIFC siheap_intcont_t *siintcont_new(sivmfnptr_t fn, address_t argc) {
  siheap_intcont_t *ic = (siheap_intcont_t *) siheap_malloc(sizeof(siheap_intcont_t) + argc*sizeof(sinanbox_t), sitype_intcont);
  ic->argc = argc;
  ic->fn = fn;
  return ic;
}

PYNTER_INLINEIFC void siintcont_destroy(siheap_intcont_t *ic) {
  for (address_t i = 0; i < ic->argc; ++i) {
    siheap_derefbox(ic->argv[i]);
  }
}
#endif

#ifdef __cplusplus
}
#endif

#endif
