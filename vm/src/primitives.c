#include <pynter/config.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include <pynter/nanbox.h>
#include <pynter/fault.h>
#include <pynter/heap.h>
#include <pynter/stack.h>
#include <pynter/debug.h>
#include <pynter/internal_fn.h>
#include <pynter/vm.h>
#include <pynter/display.h>

/**
 * This file contains the implementations (in C) of all 92 functions in the Source
 * standard library, plus a handful of py-slang (Python frontend) additions
 * appended at the end (see SIVMFN_PRIMITIVE_COUNT in internal_fn.h).
 */

static void debug_display_argv(unsigned int argc, sinanbox_t *argv) {
  (void) argc; (void) argv;
  for (unsigned int i = 0; i < argc; ++i) {
    SIDEBUG("%d: ", i);
    SIDEBUG_NANBOX(argv[i]);
    SIDEBUG("\n");
  }
}

static void handle_display(unsigned int argc, sinanbox_t *argv, bool is_error) {
  // Python's print(*args): every argument in order, space-separated,
  // followed by a newline (pynter_printer_flush below) — including the
  // zero-argument case, which still prints a bare blank line. This
  // replaces Source's original display(value, label) convention this
  // function historically implemented (a single optional label argument,
  // printed *before* the value, capped at 2 arguments total): py-slang
  // exposes only "print", with "display" as a bare alias sharing the same
  // primitive index (see PRIMITIVE_FUNCTIONS in builtins.ts) — there is no
  // py-slang-visible label-argument convention to preserve, and Python's
  // print() takes any number of positional arguments, not just up to 2.
  for (unsigned int i = 0; i < argc; ++i) {
    if (i > 0) {
      SIVMFN_PRINT(" ", is_error);
    }
    sidisplay_nanbox(argv[i], is_error);
  }

  if (pynter_printer_flush) {
    pynter_printer_flush(is_error);
  }
}

#define CHECK_ARGC(n) do { \
  if (argc < (n)) { \
    sifault(pynter_fault_function_arity); \
    return NANBOX_OFEMPTY(); \
  } \
} while (0)

/******************************************************************************
 * Basic type-checking primitives
 ******************************************************************************/

static sinanbox_t sivmfn_prim_is_array(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;
  return NANBOX_OFBOOL(NANBOX_ISPTR(v) && ((siheap_header_t *) SIHEAP_NANBOXTOPTR(v))->type == sitype_array);
}

static sinanbox_t sivmfn_prim_is_boolean(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISBOOL(*argv));
}

static sinanbox_t sivmfn_prim_is_function(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;

  if (NANBOX_ISPTR(v)) {
    siheap_type_t type = ((siheap_header_t *) SIHEAP_NANBOXTOPTR(v))->type;
    return NANBOX_OFBOOL(type == sitype_function || type == sitype_intcont);
  }

  return NANBOX_OFBOOL(NANBOX_ISIFN(v));
}

static sinanbox_t sivmfn_prim_is_null(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISNULL(*argv));
}

static sinanbox_t sivmfn_prim_is_number(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISNUMERIC(*argv));
}

static sinanbox_t sivmfn_prim_is_string(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;
  return NANBOX_OFBOOL(NANBOX_ISPTR(v) && siheap_is_string(SIHEAP_NANBOXTOPTR(v)));
}

static sinanbox_t sivmfn_prim_is_undefined(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISUNDEF(*argv));
}

/**
 * py-slang (Python frontend) additions: Python's numeric tower distinguishes
 * int and float at the type-predicate level (is_integer()/is_float()), unlike
 * Source's single is_number(). is_complex() always reports false: Pynter has
 * no complex-number representation, so a value can never be one.
 */
static sinanbox_t sivmfn_prim_is_integer(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISINT(*argv));
}

static sinanbox_t sivmfn_prim_is_float(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(NANBOX_ISFLOAT(*argv));
}

static sinanbox_t sivmfn_prim_is_complex(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  (void) argv;
  return NANBOX_OFBOOL(false);
}

/**
 * arity(f): returns the number of parameters f expects. Backs py-slang's
 * misc.ts `arity()` builtin, used by e.g. stream.prelude.ts's `is_stream` to
 * check that a stream's tail is a nullary function. A continuation
 * (sitype_intcont, e.g. a stream's lazily-built tail) is always invoked with
 * 0 arguments by convention, so it always reports arity 0.
 */
static sinanbox_t sivmfn_prim_arity(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;

  if (NANBOX_ISPTR(v)) {
    siheap_header_t *obj = SIHEAP_NANBOXTOPTR(v);
    if (obj->type == sitype_function) {
      return NANBOX_OFINT(((siheap_function_t *) obj)->code->num_args);
    }
    if (obj->type == sitype_intcont) {
      return NANBOX_OFINT(0);
    }
  }

  sifault(pynter_fault_type);
  return NANBOX_OFEMPTY();
}

/**
 * gen_list(n): allocates an n-element array pre-filled with None. Backs
 * py-slang's list.prelude.ts `_gen_list` helper (used by `build_list` to
 * allocate before filling each slot via ordinary subscript assignment) —
 * there's no existing Source-library primitive for "array of N nones".
 */
static sinanbox_t sivmfn_prim_gen_list(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  int32_t n = NANBOX_TOI32(argv[0]);
  // Reject negative sizes, and cap at NANBOX_INTMAX (consistent with other
  // collection-size limits in the VM, e.g. stream_reverse's index check
  // below): an unbounded n's allocation size (alloc_size * sizeof(sinanbox_t)
  // in siarray_new) can overflow, turning a huge request into a small
  // allocation and a heap buffer overflow when writing to it.
  if (n < 0 || n > NANBOX_INTMAX) {
    sifault(pynter_fault_type);
    return NANBOX_OFEMPTY();
  }

  // Populate the backing storage directly rather than through siarray_put:
  // the array is freshly allocated to exactly this capacity, so the
  // per-element bounds/reallocation checks siarray_put does are pure
  // overhead here — set count once at the end instead.
  siheap_array_t *arr = siarray_new(n);
  for (int32_t i = 0; i < n; ++i) {
    arr->data->data[i] = NANBOX_OFNULL();
  }
  arr->count = n;

  return SIHEAP_PTRTONANBOX(arr);
}

/******************************************************************************
 * Math primitives
 ******************************************************************************/

#define MATH_FN(name) static sinanbox_t sivmfn_prim_math_ ## name(uint8_t argc, sinanbox_t *argv) { \
  CHECK_ARGC(1); \
  return NANBOX_OFFLOAT(name ## f(NANBOX_TOFLOAT(*argv))); \
}

#define MATH_FN_2(name) static sinanbox_t sivmfn_prim_math_ ## name(uint8_t argc, sinanbox_t *argv) { \
  CHECK_ARGC(2); \
  return NANBOX_OFFLOAT(name ## f(NANBOX_TOFLOAT(argv[0]), NANBOX_TOFLOAT(argv[1]))); \
}

// Like MATH_FN, but for functions with a restricted domain (matching
// CPython's own per-function ValueError checks, e.g. math.sqrt(-1),
// math.acos(2)) — `domain_reject_expr` is a boolean C "is this out of
// domain" expression over the local `float x`, checked before calling the
// underlying libm function; true raises pynter_fault_value_error instead of
// silently propagating libm's own NaN result (e.g. sqrtf(-1) == NaN), which
// real Python raises ValueError for instead of returning nan. Must be
// phrased as a reject (not an accept) condition: comparisons against NaN are
// always false in C, so a reject-style check (`x < 0.0f`) is false for NaN
// (correctly falling through to e.g. sqrtf(nan) == nan, matching CPython's
// math.sqrt(nan) == nan), whereas its logical negation as an accept-style
// check (`!(x >= 0.0f)`) would be true for NaN and wrongly raise instead.
#define MATH_FN_DOMAIN(name, domain_reject_expr) static sinanbox_t sivmfn_prim_math_ ## name(uint8_t argc, sinanbox_t *argv) { \
  CHECK_ARGC(1); \
  float x = NANBOX_TOFLOAT(*argv); \
  if (domain_reject_expr) { \
    sifault(pynter_fault_value_error); \
  } \
  return NANBOX_OFFLOAT(name ## f(x)); \
}

MATH_FN_DOMAIN(acos, x < -1.0f || x > 1.0f)
MATH_FN_DOMAIN(acosh, x < 1.0f)
MATH_FN_DOMAIN(asin, x < -1.0f || x > 1.0f)
MATH_FN(asinh)
MATH_FN(atan)
MATH_FN_DOMAIN(atanh, x <= -1.0f || x >= 1.0f)
MATH_FN(cbrt)
MATH_FN(cos)
MATH_FN(cosh)
MATH_FN(exp)
MATH_FN(expm1)
MATH_FN_DOMAIN(log1p, x <= -1.0f)
MATH_FN_DOMAIN(log2, x <= 0.0f)
MATH_FN_DOMAIN(log10, x <= 0.0f)
MATH_FN(sin)
MATH_FN(sinh)
MATH_FN_DOMAIN(sqrt, x < 0.0f)
MATH_FN(tan)
MATH_FN(tanh)
MATH_FN_2(atan2)
MATH_FN_2(pow)

// math.log(x) / math.log(x, base) — the only math_* function with an
// optional second argument among the ones pynter implements natively (see
// math.ts's @Validate(1, 2, "math_log", true)), so it can't use MATH_FN.
static sinanbox_t sivmfn_prim_math_log(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  float x = NANBOX_TOFLOAT(argv[0]);
  if (x <= 0.0f) {
    sifault(pynter_fault_value_error);
  }
  if (argc == 1) {
    return NANBOX_OFFLOAT(logf(x));
  }
  float base = NANBOX_TOFLOAT(argv[1]);
  if (base <= 0.0f) {
    sifault(pynter_fault_value_error);
  }
  return NANBOX_OFFLOAT(logf(x) / logf(base));
}

// M_PI isn't standard C (only conditionally exposed by <math.h> depending on
// feature-test macros -- present on macOS unconditionally, but undeclared
// under CI's stricter Linux/Emscripten builds), so it's spelled out here
// instead of relied on.
#define PYNTER_PI 3.14159265358979323846f

// math.degrees(x)/math.radians(x): simple linear conversions, no native
// counterpart until now (see builtins.ts's PRIMITIVE_FUNCTIONS comment).
static sinanbox_t sivmfn_prim_math_degrees(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  float x = NANBOX_TOFLOAT(*argv);
  return NANBOX_OFFLOAT(x * 180.0f / PYNTER_PI);
}

static sinanbox_t sivmfn_prim_math_radians(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  float x = NANBOX_TOFLOAT(*argv);
  return NANBOX_OFFLOAT(x * PYNTER_PI / 180.0f);
}

static sinanbox_t sivmfn_prim_math_erf(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(erff(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_erfc(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(erfcf(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_fabs(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(fabsf(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_fma(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(3);
  float x = NANBOX_TOFLOAT(argv[0]);
  float y = NANBOX_TOFLOAT(argv[1]);
  float z = NANBOX_TOFLOAT(argv[2]);
  return NANBOX_OFFLOAT(fmaf(x, y, z));
}

static sinanbox_t sivmfn_prim_math_fmod(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  float x = NANBOX_TOFLOAT(argv[0]);
  float y = NANBOX_TOFLOAT(argv[1]);
  if (y == 0.0f) {
    sifault(pynter_fault_value_error);
  }
  return NANBOX_OFFLOAT(fmodf(x, y));
}

static sinanbox_t sivmfn_prim_math_remainder(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  float x = NANBOX_TOFLOAT(argv[0]);
  float y = NANBOX_TOFLOAT(argv[1]);
  if (y == 0.0f) {
    sifault(pynter_fault_value_error);
  }
  return NANBOX_OFFLOAT(remainderf(x, y));
}

static sinanbox_t sivmfn_prim_math_copysign(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  float x = NANBOX_TOFLOAT(argv[0]);
  float y = NANBOX_TOFLOAT(argv[1]);
  return NANBOX_OFFLOAT(copysignf(x, y));
}

static sinanbox_t sivmfn_prim_math_isfinite(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(isfinite(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_isinf(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(isinf(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_isnan(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFBOOL(isnan(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_ldexp(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  float x = NANBOX_TOFLOAT(argv[0]);
  if (!NANBOX_ISINT(argv[1])) {
    sifault(pynter_fault_type);
  }
  int32_t exponent = NANBOX_TOI32(argv[1]);
  return NANBOX_OFFLOAT(ldexpf(x, exponent));
}

static sinanbox_t sivmfn_prim_math_exp2(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(exp2f(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_gamma(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(tgammaf(NANBOX_TOFLOAT(*argv)));
}

static sinanbox_t sivmfn_prim_math_lgamma(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_OFFLOAT(lgammaf(NANBOX_TOFLOAT(*argv)));
}

// gcd/lcm/comb/factorial/isqrt/perm operate on Python `int`s specifically —
// unlike the float-domain math_* functions above, a float (or bool) argument
// here is a TypeError in real Python (math.gcd(3.0, 6) raises even though
// 3.0 is whole-valued), so these reject via NANBOX_ISINT explicitly rather
// than NANBOX_TOFLOAT/TOI32's float-coercing conversion. This deliberately
// does *not* special-case a whole-valued float (e.g. accepting 3.0 because
// truncf(3.0f) == 3.0f) purely because a genuine Python int outside this
// VM's 21-bit small-int range would otherwise also be represented as a
// float internally (see NANBOX_WRAP_INT) and get wrongly let through:
// py-slang's own CSE reference (math.ts) rejects every float argument here
// unconditionally, and matching that takes priority over handling ints past
// pynter's native range, which none of the current test cases exercise
// anyway.
static int32_t pynter_gcd_i32(int32_t a, int32_t b) {
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  while (b != 0) {
    int32_t t = a % b;
    a = b;
    b = t;
  }
  return a;
}

static sinanbox_t sivmfn_prim_math_gcd(uint8_t argc, sinanbox_t *argv) {
  int32_t result = 0;
  for (uint8_t i = 0; i < argc; ++i) {
    if (!NANBOX_ISINT(argv[i])) {
      sifault(pynter_fault_type);
    }
    result = pynter_gcd_i32(result, NANBOX_TOI32(argv[i]));
  }
  // gcd's result is bounded by the smallest input's magnitude, which is
  // itself already a valid small int (NANBOX_ISINT-checked above) -- never
  // out of NANBOX_WRAP_INT's range, but using it anyway for consistency
  // with lcm/comb/factorial/isqrt/perm below, all of which can genuinely
  // overflow it.
  return NANBOX_WRAP_INT(result);
}

static sinanbox_t sivmfn_prim_math_lcm(uint8_t argc, sinanbox_t *argv) {
  // int64_t intermediate: unlike gcd, lcm(a, b) can be as large as a*b,
  // which can overflow int32_t during the multiply even when the final
  // result (after dividing by gcd) would fit -- and can also legitimately
  // exceed NANBOX_INTMAX for larger inputs, unlike gcd.
  int64_t result = 1;
  for (uint8_t i = 0; i < argc; ++i) {
    if (!NANBOX_ISINT(argv[i])) {
      sifault(pynter_fault_type);
    }
    int32_t v = NANBOX_TOI32(argv[i]);
    if (v < 0) v = -v;
    if (v == 0 || result == 0) {
      result = 0;
      continue;
    }
    int32_t g = pynter_gcd_i32((int32_t) result, v);
    result = (result / g) * v;
  }
  return NANBOX_WRAP_INT(result);
}

static sinanbox_t sivmfn_prim_math_comb(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (!NANBOX_ISINT(argv[0]) || !NANBOX_ISINT(argv[1])) {
    sifault(pynter_fault_type);
  }
  int32_t n = NANBOX_TOI32(argv[0]);
  int32_t k = NANBOX_TOI32(argv[1]);
  if (n < 0 || k < 0) {
    sifault(pynter_fault_value_error);
  }
  if (k > n) {
    return NANBOX_OFINT(0);
  }
  if (k > n - k) {
    k = n - k;
  }
  // int64_t intermediate: the running product (before dividing by i+1 each
  // step) can exceed int32_t range well before the final binomial
  // coefficient does.
  int64_t result = 1;
  for (int32_t i = 0; i < k; ++i) {
    result = result * (n - i) / (i + 1);
  }
  return NANBOX_WRAP_INT(result);
}

static sinanbox_t sivmfn_prim_math_factorial(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  if (!NANBOX_ISINT(argv[0])) {
    sifault(pynter_fault_type);
  }
  int32_t n = NANBOX_TOI32(argv[0]);
  if (n < 0) {
    sifault(pynter_fault_value_error);
  }
  // double intermediate: factorial grows past int32_t range at n=13 and
  // past NANBOX_INTMAX (2^20-1) even sooner (n=9) -- a double keeps this
  // exact up to n=18 or so (2^53 mantissa) and an approximate float32
  // result (via NANBOX_OFFLOAT's fallback below) beyond that, matching how
  // this VM already approximates any Python int past its native range.
  double result = 1.0;
  for (int32_t i = 2; i <= n; ++i) {
    result *= i;
  }
  if (result >= NANBOX_INTMIN && result <= NANBOX_INTMAX) {
    return NANBOX_OFINT((int32_t) result);
  }
  return NANBOX_OFFLOAT((float) result);
}

static sinanbox_t sivmfn_prim_math_isqrt(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  if (!NANBOX_ISINT(argv[0])) {
    sifault(pynter_fault_type);
  }
  int32_t n = NANBOX_TOI32(argv[0]);
  if (n < 0) {
    sifault(pynter_fault_value_error);
  }
  if (n < 2) {
    return NANBOX_WRAP_INT(n);
  }
  int32_t low = 1, high = n;
  while (low < high) {
    int32_t mid = low + (high - low + 1) / 2;
    int64_t sq = (int64_t) mid * (int64_t) mid;
    if (sq <= n) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }
  // isqrt(n) <= n, and n is already a valid small int (NANBOX_ISINT-checked
  // above), so this can never actually exceed NANBOX_WRAP_INT's range --
  // used anyway for consistency, same as gcd above.
  return NANBOX_WRAP_INT(low);
}

static sinanbox_t sivmfn_prim_math_perm(uint8_t argc, sinanbox_t *argv) {
  if (argc != 1 && argc != 2) {
    sifault(pynter_fault_function_arity);
  }
  if (!NANBOX_ISINT(argv[0])) {
    sifault(pynter_fault_type);
  }
  int32_t n = NANBOX_TOI32(argv[0]);
  int32_t k = n;
  if (argc == 2 && !NANBOX_ISNULL(argv[1])) {
    if (!NANBOX_ISINT(argv[1])) {
      sifault(pynter_fault_type);
    }
    k = NANBOX_TOI32(argv[1]);
  }
  if (n < 0 || k < 0) {
    sifault(pynter_fault_value_error);
  }
  if (k > n) {
    return NANBOX_OFINT(0);
  }
  // double intermediate, same reasoning as factorial above.
  double result = 1.0;
  for (int32_t i = 0; i < k; ++i) {
    result *= (n - i);
  }
  if (result >= NANBOX_INTMIN && result <= NANBOX_INTMAX) {
    return NANBOX_OFINT((int32_t) result);
  }
  return NANBOX_OFFLOAT((float) result);
}

static sinanbox_t sivmfn_prim_math_hypot(uint8_t argc, sinanbox_t *argv) {
  // Adapted from https://github.com/v8/v8/blob/master/src/builtins/math.tq#L405
  if (argc == 0) {
    return NANBOX_OFINT(0);
  }

  float max = 0;
  for (unsigned int i = 0; i < argc; ++i) {
    if (NANBOX_IDENTICAL(argv[i], NANBOX_CANONICAL_NAN)) {
      return NANBOX_CANONICAL_NAN;
    }
    float v = NANBOX_TOFLOAT(argv[i]);
    if (v > max) {
      max = v;
    }
  }

  if (max == 0.0f) {
    return NANBOX_OFINT(0);
  }

  float sum = 0;
  float compensation = 0;
  for (unsigned int i = 0; i < argc; ++i) {
    float n = NANBOX_TOFLOAT(argv[i]) / max;
    float summand = n * n - compensation;
    float preliminary = sum + summand;
    compensation = (preliminary - sum) - summand;
    sum = preliminary;
  }

  return NANBOX_OFFLOAT(sqrtf(sum) * max);
}

static sinanbox_t sivmfn_prim_math_abs(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;

  if (NANBOX_ISINT(v)) {
    return NANBOX_WRAP_INT(abs(NANBOX_INT(v)));
  } else if (NANBOX_ISFLOAT(v)) {
    return NANBOX_OFFLOAT(fabsf(NANBOX_FLOAT(v)));
  }

  sifault(pynter_fault_type);
  return NANBOX_OFEMPTY();
}

static sinanbox_t sivmfn_prim_math_clz32(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  uint32_t clzarg = NANBOX_TOU32(*argv);

  if (clzarg == 0) {
    return NANBOX_OFINT(32);
  }

  unsigned int ret;
#if UINT_MAX == UINT32_MAX
  ret = __builtin_clz(clzarg);
#elif ULONG_MAX == UINT32_MAX
  ret = __builtin_clzl(clzarg);
#elif ULLONG_MAX == UINT32_MAX
  ret = __builtin_clzll(clzarg);
#endif
  return NANBOX_OFINT(ret);
}

static sinanbox_t sivmfn_prim_math_fround(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  // no-op: pynter uses floats not doubles
  return *argv;
}

static sinanbox_t sivmfn_prim_math_imul(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  uint32_t r = NANBOX_TOU32(argv[0])*NANBOX_TOU32(argv[1]);
  if (r > INT32_MAX) {
    r -= 0x80000000ull;
  }
  return NANBOX_WRAP_INT((int32_t) r);
}

static sinanbox_t sivmfn_prim_math_max(uint8_t argc, sinanbox_t *argv) {
  if (argc == 0) {
    return NANBOX_OFFLOAT(-INFINITY);
  }

  sinanbox_t max = argv[0];
  if (NANBOX_IDENTICAL(max, NANBOX_CANONICAL_NAN)) {
    return max;
  }
  if (!NANBOX_ISFLOAT(max) && !NANBOX_ISINT(max)) {
    sifault(pynter_fault_type);
    return max;
  }
  for (unsigned int i = 1; i < argc; ++i) {
    sinanbox_t contender = argv[i];
    if (NANBOX_IDENTICAL(contender, NANBOX_CANONICAL_NAN)) {
      return contender;
    }
    if (NANBOX_ISINT(max) && NANBOX_ISINT(contender)) {
      int32_t maxi = NANBOX_INT(max);
      int32_t contenderi = NANBOX_INT(contender);
      if (contenderi > maxi) {
        max = contender;
      }
    } else if (NANBOX_ISFLOAT(contender) || NANBOX_ISINT(contender)) {
      float maxf = NANBOX_TOFLOAT(max);
      float contenderf = NANBOX_TOFLOAT(contender);
      if (contenderf > maxf) {
        max = contender;
      }
    } else {
      sifault(pynter_fault_type);
    }
  }

  return max;
}

static sinanbox_t sivmfn_prim_math_min(uint8_t argc, sinanbox_t *argv) {
  if (argc == 0) {
    return NANBOX_OFFLOAT(INFINITY);
  }

  sinanbox_t min = argv[0];
  if (NANBOX_IDENTICAL(min, NANBOX_CANONICAL_NAN)) {
    return min;
  }
  if (!NANBOX_ISFLOAT(min) && !NANBOX_ISINT(min)) {
    sifault(pynter_fault_type);
    return min;
  }
  for (unsigned int i = 1; i < argc; ++i) {
    sinanbox_t contender = argv[i];
    if (NANBOX_IDENTICAL(contender, NANBOX_CANONICAL_NAN)) {
      return contender;
    }
    if (NANBOX_ISINT(min) && NANBOX_ISINT(contender)) {
      int32_t mini = NANBOX_INT(min);
      int32_t contenderi = NANBOX_INT(contender);
      if (contenderi < mini) {
        min = contender;
      }
    } else if (NANBOX_ISFLOAT(contender) || NANBOX_ISINT(contender)) {
      float minf = NANBOX_TOFLOAT(min);
      float contenderf = NANBOX_TOFLOAT(contender);
      if (contenderf < minf) {
        min = contender;
      }
    } else {
      sifault(pynter_fault_type);
    }
  }

  return min;
}
static sinanbox_t sivmfn_prim_math_random(uint8_t argc, sinanbox_t *argv) {
  (void) argc; (void) argv;
  return NANBOX_OFFLOAT((float) rand() / (((float) RAND_MAX) + 1.0f));
}

static sinanbox_t sivmfn_prim_math_sign(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;
  if (NANBOX_ISINT(v)) {
    int32_t intv = NANBOX_INT(v);
    return NANBOX_OFINT(intv ? (intv > 0 ? 1 : -1) : 0);
  } else if (NANBOX_ISFLOAT(v)) {
    float fv = NANBOX_FLOAT(v);
    return NANBOX_OFINT(fv != 0.0f ? (fv > 0.0f ? 1 : -1) : 0);
  } else {
    sifault(pynter_fault_type);
  }
  return NANBOX_OFEMPTY();
}

#define FLOAT_ROUND_FN(name) static sinanbox_t sivmfn_prim_math_## name(uint8_t argc, sinanbox_t *argv) { \
  CHECK_ARGC(1); \
  sinanbox_t v = *argv; \
  if (NANBOX_ISINT(v)) { \
    return v; \
  } else if (NANBOX_ISFLOAT(v)) { \
    float retv = name ## f(NANBOX_FLOAT(v)); \
    if (retv >= NANBOX_INTMIN && retv <= NANBOX_INTMAX) { \
      return NANBOX_OFINT((int32_t) retv); \
    } \
    return NANBOX_OFFLOAT(retv); \
  } else { \
    sifault(pynter_fault_type); \
  } \
  return NANBOX_OFEMPTY(); \
}

FLOAT_ROUND_FN(floor)
FLOAT_ROUND_FN(ceil)
FLOAT_ROUND_FN(round)
FLOAT_ROUND_FN(trunc)

/******************************************************************************
 * Pair primitives
 ******************************************************************************/

static inline siheap_array_t *source_pair_ptr(sinanbox_t l, sinanbox_t r) {
  siheap_array_t *arr = siarray_new(2);
  siarray_put(arr, 0, l);
  siarray_put(arr, 1, r);
  return arr;
}

static inline sinanbox_t source_pair(sinanbox_t l, sinanbox_t r) {
  return SIHEAP_PTRTONANBOX(source_pair_ptr(l, r));
}

static inline siheap_array_t *nanbox_toarray(sinanbox_t p) {
  siheap_header_t *v = SIHEAP_NANBOXTOPTR(p);
  siheap_array_t *a = (siheap_array_t *) v;
  if (!NANBOX_ISPTR(p) || v->type != sitype_array) {
    sifault(pynter_fault_type);
    return NULL;
  }
  return a;
}

static inline sinanbox_t source_head(sinanbox_t p) {
  siheap_array_t *a = nanbox_toarray(p);
  return siarray_get(a, 0);
}

static inline sinanbox_t source_tail(sinanbox_t p) {
  siheap_array_t *a = nanbox_toarray(p);
  return siarray_get(a, 1);
}

static sinanbox_t sivmfn_prim_pair(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  siheap_refbox(argv[0]);
  siheap_refbox(argv[1]);
  return source_pair(argv[0], argv[1]);
}

static sinanbox_t sivmfn_prim_head(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t ret = source_head(argv[0]);
  siheap_refbox(ret);
  return ret;
}

static sinanbox_t sivmfn_prim_tail(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t ret = source_tail(argv[0]);
  siheap_refbox(ret);
  return ret;
}

static sinanbox_t sivmfn_prim_set_head(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  siheap_array_t *a = nanbox_toarray(argv[0]);
  siheap_refbox(argv[1]);
  siarray_put(a, 0, argv[1]);
  return NANBOX_OFUNDEF();
}

static sinanbox_t sivmfn_prim_set_tail(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  siheap_array_t *a = nanbox_toarray(argv[0]);
  siheap_refbox(argv[1]);
  siarray_put(a, 1, argv[1]);
  return NANBOX_OFUNDEF();
}

static sinanbox_t sivmfn_prim_is_pair(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  siheap_header_t *v = SIHEAP_NANBOXTOPTR(argv[0]);
  siheap_array_t *a = (siheap_array_t *) v;
  return NANBOX_OFBOOL(NANBOX_ISPTR(argv[0]) && v->type == sitype_array && a->count == 2);
}

/******************************************************************************
 * List primitives
 ******************************************************************************/

static sinanbox_t sivmfn_prim_is_list(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t l = argv[0];
  while (!NANBOX_ISNULL(l)) {
    siheap_header_t *v = SIHEAP_NANBOXTOPTR(l);
    siheap_array_t *a = (siheap_array_t *) v;
    if (!NANBOX_ISPTR(l) || v->type != sitype_array || a->count != 2) {
      return NANBOX_OFBOOL(false);
    }
    l = siarray_get(a, 1);
  }

  return NANBOX_OFBOOL(true);
}

static inline size_t source_list_length(sinanbox_t l) {
  size_t length = 0;
  while (!NANBOX_ISNULL(l)) {
    ++length;
    l = source_tail(l);
  }
  return length;
}

static sinanbox_t sivmfn_prim_accumulate(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(3);

  // this function is particularly horrible for us
  // we don't want to have a naive recursive implementation (in case we blow the C stack
  // the Pynter stack isn't particularly large either
  // so.. here's a compromise: we allocate an array and flatten the list into it
  // then we accumulate using the array

  sinanbox_t f = argv[0], acc = argv[1];
  const size_t list_length = source_list_length(argv[2]);
  siheap_array_t *flat_list = siarray_new(list_length);
  siheap_intref(flat_list);
  // flatten the list into the array
  {
    size_t idx = 0;
    sinanbox_t l = argv[2];
    while (!NANBOX_ISNULL(l)) {
      siheap_array_t *pair = nanbox_toarray(l);
      sinanbox_t head = siarray_get(pair, 0);
      siheap_refbox(head);
      siarray_put(flat_list, idx, head);
      l = siarray_get(pair, 1);
      idx += 1;
    }
    assert(idx == list_length);
  }

  // the initial acc has a ref from the caller's stack (it's not removed until
  // after we return. if we are passing it to a reentrant call, then there will
  // be a second ref from our callee's env
  // ref it
  siheap_refbox(acc);
  for (size_t idxp1 = list_length; idxp1 > 0; --idxp1) {
    sinanbox_t f_args[] = {siarray_get(flat_list, idxp1 - 1), acc};
    siheap_refbox(f_args[0]);
    acc = siexec_nanbox(f, 2, f_args);
  }

  siheap_intderef(flat_list);
  siheap_deref(flat_list);

  return acc;
}

static sinanbox_t sivmfn_prim_append(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  sinanbox_t list = argv[0];

  if (NANBOX_ISNULL(list)) {
    siheap_refbox(argv[1]);
    return argv[1];
  }

  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t head = siarray_get(pair, 0);
    list = siarray_get(pair, 1);
    siheap_refbox(head);
    siheap_array_t *new_pair = source_pair_ptr(head, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
    }
    prev_pair = new_pair;
  }

  siheap_refbox(argv[1]);
  siarray_put(prev_pair, 1, argv[1]);
  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_build_list(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  int32_t limit = NANBOX_TOI32(argv[0]);

  if (limit <= 0) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;

  for (int32_t i = 0; i < limit; ++i) {
    sinanbox_t arg = NANBOX_WRAP_INT(i);
    sinanbox_t new_val = siexec_nanbox(argv[1], 1, &arg);
    siheap_array_t *new_pair = source_pair_ptr(new_val, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  siheap_intderef(new_list);
  return SIHEAP_PTRTONANBOX(new_list);
}

#define PRIM_ENUM_LIST_FN(type, each) static inline sinanbox_t enum_list_##type(type start, type end) { \
  if (start > end) { \
    return NANBOX_OFNULL(); \
  } \
  siheap_array_t *new_list = NULL; \
  siheap_array_t *prev_pair = NULL; \
  for (type i = start; i <= end; i += 1) { \
    siheap_array_t *new_pair = source_pair_ptr(each, NANBOX_OFNULL()); \
    if (prev_pair) { \
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair)); \
    } \
    if (!new_list) { \
      new_list = new_pair; \
    } \
    prev_pair = new_pair; \
  } \
  return SIHEAP_PTRTONANBOX(new_list); \
}

PRIM_ENUM_LIST_FN(int32_t, NANBOX_WRAP_INT(i))
PRIM_ENUM_LIST_FN(float, NANBOX_OFFLOAT(i))

static sinanbox_t sivmfn_prim_enum_list(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  if (NANBOX_ISINT(argv[0])) {
      if (NANBOX_ISINT(argv[1])) {
        return enum_list_int32_t(NANBOX_INT(argv[0]), NANBOX_INT(argv[1]));
      } else if (NANBOX_ISFLOAT(argv[1])) {
        float end = NANBOX_FLOAT(argv[1]);
        if (end > INT32_MIN && end < (float) INT32_MAX) {
          return enum_list_int32_t(NANBOX_TOI32(argv[0]), (int32_t) end);
        } else {
          return enum_list_float(NANBOX_INT(argv[0]), end);
        }
      }
  } else if (NANBOX_ISFLOAT(argv[0])) {
    return enum_list_float(NANBOX_FLOAT(argv[0]), NANBOX_TOFLOAT(argv[1]));
  }
  sifault(pynter_fault_type);
  return NANBOX_OFEMPTY();
}

static sinanbox_t sivmfn_prim_filter(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t filter_fn = argv[0];

  sinanbox_t old_list = argv[1];
  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  while (!NANBOX_ISNULL(old_list)) {
    siheap_array_t *pair = nanbox_toarray(old_list);
    sinanbox_t cur = siarray_get(pair, 0);
    old_list = siarray_get(pair, 1);
    siheap_refbox(cur);
    sinanbox_t pred_result = siexec_nanbox(filter_fn, 1, &cur);
    if (NANBOX_ISBOOL(pred_result)) {
      if (!NANBOX_BOOL(pred_result)) {
        continue;
      }
    } else {
      sifault(pynter_fault_type);
    }
    siheap_refbox(cur);
    siheap_array_t *new_pair = source_pair_ptr(cur, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  if (new_list) {
    siheap_intderef(new_list);
    return SIHEAP_PTRTONANBOX(new_list);
  }

  return NANBOX_OFNULL();
}

static sinanbox_t sivmfn_prim_for_each(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t for_each_fn = argv[0];

  sinanbox_t list = argv[1];
  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t cur = siarray_get(pair, 0);
    list = siarray_get(pair, 1);
    siheap_refbox(cur);
    siheap_derefbox(siexec_nanbox(for_each_fn, 1, &cur));
  }

  return NANBOX_OFUNDEF();
}

static sinanbox_t sivmfn_prim_length(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return NANBOX_WRAP_UINT(source_list_length(argv[0]));
}

static sinanbox_t sivmfn_prim_list(uint8_t argc, sinanbox_t *argv) {
  if (argc == 0) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;

  for (size_t i = 0; i < argc; ++i) {
    siheap_refbox(argv[i]);
    siheap_array_t *new_pair = source_pair_ptr(argv[i], NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
    }
    prev_pair = new_pair;
  }

  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_list_ref(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  sinanbox_t list = argv[0];
  int32_t count = NANBOX_TOI32(argv[1]);
  for (int32_t i = 0; i < count; ++i) {
    list = source_tail(list);
  }

  sinanbox_t retv = source_head(list);
  siheap_refbox(retv);
  return retv;
}

static sinanbox_t sivmfn_prim_map(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t map_fn = argv[0];

  sinanbox_t old_list = argv[1];
  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  while (!NANBOX_ISNULL(old_list)) {
    siheap_array_t *pair = nanbox_toarray(old_list);
    sinanbox_t cur = siarray_get(pair, 0);
    old_list = siarray_get(pair, 1);
    siheap_refbox(cur);
    sinanbox_t newval = siexec_nanbox(map_fn, 1, &cur);
    siheap_array_t *new_pair = source_pair_ptr(newval, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  siheap_intderef(new_list);
  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_member(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t needle = argv[0];

  sinanbox_t list = argv[1];
  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t cur = siarray_get(pair, 0);
    if (sivm_equal(needle, cur)) {
      break;
    }
    list = siarray_get(pair, 1);
  }

  siheap_refbox(list);
  return list;
}

static sinanbox_t sivmfn_prim_remove(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t needle = argv[0];

  sinanbox_t list = argv[1];
  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t cur = siarray_get(pair, 0);
    list = siarray_get(pair, 1);

    if (sivm_equal(cur, needle)) {
      siheap_refbox(list);
      if (prev_pair) {
        assert(new_list);
        siarray_put(prev_pair, 1, list);
        return SIHEAP_PTRTONANBOX(new_list);
      } else {
        return list;
      }
    }

    siheap_refbox(cur);
    siheap_array_t *new_pair = source_pair_ptr(cur, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
    }
    prev_pair = new_pair;
  }

  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_remove_all(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  if (NANBOX_ISNULL(argv[1])) {
    return NANBOX_OFNULL();
  }

  const sinanbox_t needle = argv[0];

  sinanbox_t list = argv[1];
  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t cur = siarray_get(pair, 0);
    list = siarray_get(pair, 1);
    if (sivm_equal(cur, needle)) {
      continue;
    }

    siheap_refbox(cur);
    siheap_array_t *new_pair = source_pair_ptr(cur, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  if (new_list) {
    siheap_intderef(new_list);
    return SIHEAP_PTRTONANBOX(new_list);
  }
  return NANBOX_OFNULL();
}

static sinanbox_t sivmfn_prim_reverse(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t list = argv[0];
  if (NANBOX_ISNULL(list)) {
    return list;
  }

  siheap_array_t *new_list = NULL;

  while (!NANBOX_ISNULL(list)) {
    siheap_array_t *pair = nanbox_toarray(list);
    sinanbox_t cur = siarray_get(pair, 0);
    list = siarray_get(pair, 1);

    siheap_refbox(cur);
    new_list = source_pair_ptr(cur, new_list ? SIHEAP_PTRTONANBOX(new_list) : NANBOX_OFNULL());
  }

  return SIHEAP_PTRTONANBOX(new_list);
}

/******************************************************************************
 * Array primitive
 ******************************************************************************/

static sinanbox_t sivmfn_prim_array_length(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t v = *argv;
  siheap_header_t *obj = SIHEAP_NANBOXTOPTR(v);
  if (!NANBOX_ISPTR(v) || obj->type != sitype_array) {
    sifault(pynter_fault_type);
    return NANBOX_OFEMPTY();
  }

  siheap_array_t *arr = (siheap_array_t *) obj;
  return NANBOX_WRAP_INT((int) arr->count);
}

/******************************************************************************
 * Stream primitives
 ******************************************************************************/

/**
 * Applies the tail of stream, and returns the result. Equivalent to stream_tail in Source.
 *
 * References: No argument references consumed. Returns return value reference.
 */
static inline sinanbox_t source_stream_tail(sinanbox_t stream) {
  sinanbox_t tail = source_tail(stream);
  return siexec_nanbox(tail, 0, NULL);
}

static sinanbox_t sivmfn_prim_list_to_stream(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t list = argv[0];
  if (NANBOX_ISNULL(list)) {
    return list;
  }

  siheap_array_t *pair = nanbox_toarray(list);
  sinanbox_t head = siarray_get(pair, 0);
  sinanbox_t tail = siarray_get(pair, 1);
  siheap_refbox(head);
  siheap_refbox(tail);
  siheap_intcont_t *ic = siintcont_new(sivmfn_prim_list_to_stream, 1);
  ic->argv[0] = tail;
  return source_pair(head, SIHEAP_PTRTONANBOX(ic));
}

/**
 * Continuation for sivmfn_prim_build_stream.
 *
 * @param argc 2
 * @param argv <tt>{ current: number, max: number, fn: function }</tt>
 * @return <tt>pair(fn(current), intcont { current + 1, max, fn })</tt>
 */
static sinanbox_t prim_build_stream_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t fn = argv[2];
  int32_t cur = NANBOX_TOI32(argv[0]), max = NANBOX_TOI32(argv[1]);
  if (cur >= max) {
    return NANBOX_OFNULL();
  }

  sinanbox_t curv = siexec_nanbox(fn, 1, argv);
  siheap_intcont_t *ic = siintcont_new(prim_build_stream_cont, 3);
  ic->argv[0] = NANBOX_WRAP_INT(cur + 1);
  ic->argv[1] = argv[1];
  siheap_refbox(fn);
  ic->argv[2] = fn;

  return source_pair(curv, SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_build_stream(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t arg[] = { NANBOX_OFINT(0u), argv[0], argv[1] };
  return prim_build_stream_cont(3, arg);
}

static sinanbox_t sivmfn_prim_enum_stream(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t start = argv[0], end = argv[1];

  if (NANBOX_ISINT(start) && NANBOX_ISINT(end)) {
    // n.b. don't merge this into an &&, we don't want to unnecessarily convert to float
    if (NANBOX_INT(start) > NANBOX_INT(end)) {
      return NANBOX_OFNULL();
    }
  } else if (NANBOX_TOFLOAT(start) > NANBOX_TOFLOAT(end)) {
    return NANBOX_OFNULL();
  }

  siheap_intcont_t *ic = siintcont_new(sivmfn_prim_enum_stream, 2);
  if (NANBOX_ISINT(start)) {
    ic->argv[0] = NANBOX_WRAP_INT(NANBOX_INT(start) + 1);
  } else { // note: type checked by the ISINT or TOFLOAT above
    ic->argv[0] = NANBOX_OFFLOAT(NANBOX_FLOAT(start) + 1);
  }
  ic->argv[1] = end;
  return source_pair(start, SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_eval_stream(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  int32_t limit = NANBOX_TOI32(argv[1]);

  if (limit <= 0) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  sinanbox_t stream = argv[0];
  siheap_refbox(stream);

  for (int32_t i = 0; i < limit; ++i) {
    siheap_array_t *stream_pair = nanbox_toarray(stream);
    sinanbox_t new_val = siarray_get(stream_pair, 0);
    sinanbox_t stream_tail = siarray_get(stream_pair, 1);
    siheap_intref(stream_pair);
    stream = siexec_nanbox(stream_tail, 0, NULL);
    siheap_intderef(stream_pair);

    siheap_refbox(new_val);
    siheap_deref(stream_pair);

    siheap_array_t *new_pair = source_pair_ptr(new_val, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  siheap_derefbox(stream);
  siheap_intderef(new_list);
  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_integers_from(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t val = argv[0];
  siheap_intcont_t *ic = siintcont_new(sivmfn_prim_integers_from, 1);
  if (NANBOX_ISINT(val)) {
    ic->argv[0] = NANBOX_WRAP_INT(NANBOX_INT(val) + 1);
  } else {
    ic->argv[0] = NANBOX_OFFLOAT(NANBOX_TOFLOAT(val) + 1);
  }

  return source_pair(val, SIHEAP_PTRTONANBOX(ic));
}

/**
 * Continuation for sivmfn_prim_stream.
 *
 * @param argc 1 | 2
 * @param argv <tt>{ array: array, next_index?: number }</tt>
 * @return <tt>pair(array[next_index], intcont { array, next_index + 1 })</tt>
 */
static sinanbox_t prim_stream_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  // no array (this is an optimisation for a stream of 1)
  if (NANBOX_ISNULL(argv[0])) {
    return NANBOX_OFNULL();
  }

  uint32_t idx = NANBOX_TOU32(argv[1]);
  siheap_array_t *arr = SIHEAP_NANBOXTOPTR(argv[0]);

  if (idx >= arr->count) {
    return NANBOX_OFNULL();
  }

  // construct the new continuation
  siheap_intcont_t *ic = siintcont_new(prim_stream_cont, 2);
  ic->argv[0] = argv[0];
  ic->argv[1] = NANBOX_WRAP_UINT(idx + 1);

  // ref the new pair's head
  siheap_refbox(arr->data->data[idx]);
  // ref the array (since it's going into the continuation)
  siheap_ref(arr);

  return source_pair(arr->data->data[idx], SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream(uint8_t argc, sinanbox_t *argv) {
  if (!argc) {
    return NANBOX_OFNULL();
  } else if (argc == 1) {
    siheap_intcont_t *ic = siintcont_new(prim_stream_cont, 1);
    ic->argv[0] = NANBOX_OFNULL();
    siheap_refbox(argv[0]);
    return source_pair(argv[0], SIHEAP_PTRTONANBOX(ic));
  }

  for (size_t i = 0; i < argc; ++i) {
    siheap_refbox(argv[i]);
  }

  siheap_array_t *arr = siarray_new(argc - 1);
  memcpy(arr->data->data, argv + 1, (argc - 1)*sizeof(sinanbox_t));
  arr->count = argc - 1;

  siheap_intcont_t *ic = siintcont_new(prim_stream_cont, 2);
  ic->argv[0] = SIHEAP_PTRTONANBOX(arr);
  ic->argv[1] = NANBOX_OFINT(0);
  return source_pair(argv[0], SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream_append(uint8_t argc, sinanbox_t *argv);

static sinanbox_t prim_stream_append_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t tfn = argv[0], ys = argv[1];
  sinanbox_t xs = siexec_nanbox(tfn, 0, NULL);
  sinanbox_t retv = sivmfn_prim_stream_append(2, (sinanbox_t[]) { xs, ys });
  siheap_derefbox(xs);
  return retv;
}

static sinanbox_t sivmfn_prim_stream_append(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  sinanbox_t xs = argv[0], ys = argv[1];
  siheap_refbox(ys); // ref ys - either we return it, or it goes into the cont

  if (NANBOX_ISNULL(xs)) {
    return ys;
  }

  siheap_array_t *stream_pair = nanbox_toarray(argv[0]);
  sinanbox_t stream_head = siarray_get(stream_pair, 0);
  sinanbox_t stream_tail = siarray_get(stream_pair, 1);
  siheap_refbox(stream_head);
  siheap_refbox(stream_tail);

  siheap_intcont_t *ic = siintcont_new(prim_stream_append_cont, 2);
  ic->argv[0] = stream_tail;
  ic->argv[1] = ys;
  return source_pair(stream_head, SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream_filter(uint8_t argc, sinanbox_t *argv);

static sinanbox_t prim_stream_filter_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t fn = argv[0], tfn = argv[1];
  sinanbox_t xs = siexec_nanbox(tfn, 0, NULL);
  siheap_intrefbox(xs);
  sinanbox_t retv = sivmfn_prim_stream_filter(2, (sinanbox_t[]) { fn, xs });
  siheap_intderefbox(xs);
  siheap_derefbox(xs);
  return retv;
}

static sinanbox_t sivmfn_prim_stream_filter(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t fn = argv[0], xs = argv[1];

  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_refbox(xs);
  while (!NANBOX_ISNULL(xs)) {
    siheap_array_t *stream_pair = nanbox_toarray(xs);
    sinanbox_t head = siarray_get(stream_pair, 0);
    sinanbox_t tail = siarray_get(stream_pair, 1);

    siheap_intref(stream_pair);
    siheap_refbox(head);
    sinanbox_t fn_res = siexec_nanbox(fn, 1, &head);
    siheap_intderef(stream_pair);

    if (!NANBOX_ISBOOL(fn_res)) {
      sifault(pynter_fault_type);
      return NANBOX_OFEMPTY();
    }

    if (NANBOX_BOOL(fn_res)) {
      siheap_intcont_t *ic = siintcont_new(prim_stream_filter_cont, 2);
      siheap_refbox(head);
      siheap_refbox(fn);
      siheap_refbox(tail);
      ic->argv[0] = fn;
      ic->argv[1] = tail;
      siheap_deref(stream_pair);
      return source_pair(head, SIHEAP_PTRTONANBOX(ic));
    }

    siheap_intref(stream_pair);
    xs = siexec_nanbox(tail, 0, NULL);
    siheap_intderef(stream_pair);
    siheap_deref(stream_pair);
  }

  siheap_derefbox(xs);
  return NANBOX_OFNULL();
}

static sinanbox_t sivmfn_prim_stream_for_each(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);

  sinanbox_t stream = argv[1];
  if (NANBOX_ISNULL(stream)) {
    return NANBOX_OFUNDEF();
  }

  sinanbox_t fn = argv[0];

  siheap_refbox(stream);
  while (!NANBOX_ISNULL(stream)) {
    siheap_array_t *stream_pair = nanbox_toarray(stream);
    sinanbox_t head = siarray_get(stream_pair, 0);
    sinanbox_t stream_tail = siarray_get(stream_pair, 1);
    siheap_intref(stream_pair);
    siheap_refbox(head);
    siheap_derefbox(siexec_nanbox(fn, 1, &head));
    stream = siexec_nanbox(stream_tail, 0, NULL);
    siheap_intderef(stream_pair);
    siheap_deref(stream_pair);
  }
  siheap_derefbox(stream);

  return NANBOX_OFUNDEF();
}

static sinanbox_t sivmfn_prim_stream_length(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t stream = argv[0];
  if (NANBOX_ISNULL(stream)) {
    return NANBOX_OFINT(0);
  }

  size_t length = 0;
  siheap_refbox(stream);
  while (!NANBOX_ISNULL(stream)) {
    ++length;
    sinanbox_t last_stream = stream;
    siheap_intrefbox(last_stream);
    stream = source_stream_tail(last_stream);
    siheap_intderefbox(last_stream);
    siheap_derefbox(last_stream);
  }
  siheap_derefbox(stream);

  return NANBOX_WRAP_UINT(length);
}

static sinanbox_t sivmfn_prim_stream_map(uint8_t argc, sinanbox_t *argv);

static sinanbox_t prim_stream_map_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t fn = argv[0], tfn = argv[1];
  sinanbox_t xs = siexec_nanbox(tfn, 0, NULL);
  siheap_intrefbox(xs);
  sinanbox_t retv = sivmfn_prim_stream_map(2, (sinanbox_t[]) { fn, xs });
  siheap_intderefbox(xs);
  siheap_derefbox(xs);
  return retv;
}

static sinanbox_t sivmfn_prim_stream_map(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t fn = argv[0], xs = argv[1];

  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *stream_pair = nanbox_toarray(xs);
  sinanbox_t head = siarray_get(stream_pair, 0);
  sinanbox_t tail = siarray_get(stream_pair, 1);

  siheap_refbox(head);
  sinanbox_t fn_res = siexec_nanbox(fn, 1, &head);

  siheap_intcont_t *ic = siintcont_new(prim_stream_map_cont, 2);
  siheap_refbox(fn);
  siheap_refbox(tail);
  ic->argv[0] = fn;
  ic->argv[1] = tail;
  return source_pair(fn_res, SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream_member(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t needle = argv[0], xs = argv[1];

  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_refbox(xs);
  while (!NANBOX_ISNULL(xs)) {
    siheap_array_t *stream_pair = nanbox_toarray(xs);
    sinanbox_t head = siarray_get(stream_pair, 0);
    sinanbox_t tail = siarray_get(stream_pair, 1);

    if (sivm_equal(head, needle)) {
      return xs;
    }

    siheap_intref(stream_pair);
    xs = siexec_nanbox(tail, 0, NULL);
    siheap_intderef(stream_pair);
    siheap_deref(stream_pair);
  }

  siheap_derefbox(xs);
  return NANBOX_OFNULL();
}

static sinanbox_t sivmfn_prim_stream_ref(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t xs = argv[0];
  int32_t count = NANBOX_TOI32(argv[1]);

  siheap_refbox(xs);
  for (int32_t i = 0; i < count; ++i) {
    sinanbox_t old_xs = xs;
    siheap_intrefbox(old_xs);
    xs = source_stream_tail(old_xs);
    siheap_intderefbox(old_xs);
    siheap_derefbox(old_xs);
  }

  sinanbox_t head = source_head(xs);
  siheap_refbox(head);
  siheap_derefbox(xs);
  return head;
}

static sinanbox_t sivmfn_prim_stream_remove(uint8_t argc, sinanbox_t *argv);

static sinanbox_t prim_stream_remove_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t needle = argv[0], tfn = argv[1];
  sinanbox_t xs = siexec_nanbox(tfn, 0, NULL);
  siheap_intrefbox(xs);
  sinanbox_t retv = sivmfn_prim_stream_remove(2, (sinanbox_t[]) { needle, xs });
  siheap_intderefbox(xs);
  siheap_derefbox(xs);
  return retv;
}

static sinanbox_t sivmfn_prim_stream_remove(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t needle = argv[0], xs = argv[1];

  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *stream_pair = nanbox_toarray(xs);
  sinanbox_t head = siarray_get(stream_pair, 0);
  sinanbox_t tail = siarray_get(stream_pair, 1);

  if (!sivm_equal(head, needle)) {
    siheap_intcont_t *ic = siintcont_new(prim_stream_remove_cont, 2);
    siheap_refbox(head);
    siheap_refbox(needle);
    siheap_refbox(tail);
    ic->argv[0] = needle;
    ic->argv[1] = tail;
    return source_pair(head, SIHEAP_PTRTONANBOX(ic));
  }

  xs = siexec_nanbox(tail, 0, NULL);
  return xs;
}

static sinanbox_t sivmfn_prim_stream_remove_all(uint8_t argc, sinanbox_t *argv);

static sinanbox_t prim_stream_remove_all_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  sinanbox_t needle = argv[0], tfn = argv[1];
  sinanbox_t xs = siexec_nanbox(tfn, 0, NULL);
  siheap_intrefbox(xs);
  sinanbox_t retv = sivmfn_prim_stream_remove_all(2, (sinanbox_t[]) { needle, xs });
  siheap_intderefbox(xs);
  siheap_derefbox(xs);
  return retv;
}

static sinanbox_t sivmfn_prim_stream_remove_all(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  sinanbox_t needle = argv[0], xs = argv[1];

  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_refbox(xs);
  while (!NANBOX_ISNULL(xs)) {
    siheap_array_t *stream_pair = nanbox_toarray(xs);
    sinanbox_t head = siarray_get(stream_pair, 0);
    sinanbox_t tail = siarray_get(stream_pair, 1);

    if (!sivm_equal(needle, head)) {
      siheap_intcont_t *ic = siintcont_new(prim_stream_remove_all_cont, 2);
      siheap_refbox(head);
      siheap_refbox(needle);
      siheap_refbox(tail);
      ic->argv[0] = needle;
      ic->argv[1] = tail;
      siheap_deref(stream_pair);
      return source_pair(head, SIHEAP_PTRTONANBOX(ic));
    }

    siheap_intref(stream_pair);
    xs = siexec_nanbox(tail, 0, NULL);
    siheap_intderef(stream_pair);
    siheap_deref(stream_pair);
  }

  siheap_derefbox(xs);
  return NANBOX_OFNULL();
}

/**
 * Continuation for sivmfn_prim_stream_reverse.
 *
 * @param argc 2
 * @param argv <tt>{ array: array, last_index: number }</tt>
 * @return <tt>pair(array[last_index - 1], intcont { array, last_index - 1 })</tt>
 */
static sinanbox_t prim_stream_reverse_cont(uint8_t argc, sinanbox_t *argv) {
  (void) argc;
  uint32_t idx = NANBOX_INT(argv[1]);
  siheap_array_t *arr = SIHEAP_NANBOXTOPTR(argv[0]);

  if (idx == 0) {
    return NANBOX_OFNULL();
  }

  // construct the new continuation
  siheap_intcont_t *ic = siintcont_new(prim_stream_reverse_cont, 2);
  ic->argv[0] = argv[0];
  ic->argv[1] = NANBOX_OFINT(idx - 1);

  // ref the new pair's head
  siheap_refbox(arr->data->data[idx - 1]);
  // ref the array (since it's going into the continuation)
  siheap_ref(arr);

  return source_pair(arr->data->data[idx - 1], SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream_reverse(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t xs = argv[0];
  if (NANBOX_ISNULL(xs)) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *stream_array = siarray_new(4);
  siheap_intref(stream_array);

  address_t index = 0;
  siheap_refbox(xs);
  while (!NANBOX_ISNULL(xs)) {
    siheap_array_t *stream_pair = nanbox_toarray(xs);
    sinanbox_t head = siarray_get(stream_pair, 0);
    sinanbox_t tail = siarray_get(stream_pair, 1);

    siheap_refbox(head);
    siarray_put(stream_array, index++, head);

    siheap_intref(stream_pair);
    xs = siexec_nanbox(tail, 0, NULL);
    siheap_intderef(stream_pair);
    siheap_deref(stream_pair);
  }
  siheap_derefbox(xs);

  if (index - 1 > NANBOX_INTMAX) {
    // i guess streams of 0x100000 are big enough, right?
    sifault(pynter_fault_internal_error);
    return NANBOX_OFEMPTY();
  }

  sinanbox_t new_head = siarray_get(stream_array, index - 1);
  siheap_refbox(new_head);
  siheap_intcont_t *ic = siintcont_new(prim_stream_reverse_cont, 2);
  siheap_intderef(stream_array);
  ic->argv[0] = SIHEAP_PTRTONANBOX(stream_array);
  ic->argv[1] = NANBOX_OFINT(index - 1);
  return source_pair(new_head, SIHEAP_PTRTONANBOX(ic));
}

static sinanbox_t sivmfn_prim_stream_tail(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  return source_stream_tail(argv[0]);
}

static sinanbox_t sivmfn_prim_stream_to_list(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);

  sinanbox_t stream = argv[0];

  if (NANBOX_ISNULL(stream)) {
    return NANBOX_OFNULL();
  }

  siheap_array_t *new_list = NULL;
  siheap_array_t *prev_pair = NULL;
  siheap_refbox(stream);
  while (!NANBOX_ISNULL(stream)) {
    siheap_array_t *stream_pair = nanbox_toarray(stream);
    sinanbox_t new_val = siarray_get(stream_pair, 0);
    sinanbox_t stream_tail = siarray_get(stream_pair, 1);
    siheap_intref(stream_pair);
    stream = siexec_nanbox(stream_tail, 0, NULL);
    siheap_intderef(stream_pair);

    siheap_refbox(new_val);
    siheap_deref(stream_pair);

    siheap_array_t *new_pair = source_pair_ptr(new_val, NANBOX_OFNULL());
    if (prev_pair) {
      siarray_put(prev_pair, 1, SIHEAP_PTRTONANBOX(new_pair));
    }
    if (!new_list) {
      new_list = new_pair;
      siheap_intref(new_list);
    }
    prev_pair = new_pair;
  }

  siheap_derefbox(stream);
  siheap_intderef(new_list);
  return SIHEAP_PTRTONANBOX(new_list);
}

static sinanbox_t sivmfn_prim_is_stream(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(1);
  sinanbox_t xs = argv[0];

  siheap_refbox(xs);
  while (!NANBOX_ISNULL(xs)) {
    siheap_header_t *obj = SIHEAP_NANBOXTOPTR(xs);
    siheap_array_t *pair = (siheap_array_t *) obj;
    if (!NANBOX_ISPTR(xs) || obj->type != sitype_array || pair->count != 2) {
      siheap_derefbox(xs);
      return NANBOX_OFBOOL(false);
    }

    sinanbox_t tail = siarray_get(pair, 1);
    siheap_header_t *tailobj = SIHEAP_NANBOXTOPTR(tail);

    if (!NANBOX_ISIFN(tail)
        && !(NANBOX_ISPTR(tail) && (tailobj->type == sitype_function || tailobj->type == sitype_intcont))) {
      siheap_deref(pair);
      return NANBOX_OFBOOL(false);
    }

    siheap_intref(pair);
    xs = siexec_nanbox(tail, 0, NULL);
    siheap_intderef(pair);
    siheap_deref(pair);
  }

  return NANBOX_OFBOOL(true);
}

/******************************************************************************
 * Iterator primitives
 ******************************************************************************/

/**
 * range(stop) / range(start, stop) / range(start, stop, step) — the only
 * iterable py-slang's `for` loop grammar ever compiles at this VM's target
 * chapter (§3's ForRangeOnlyValidator), so this is also the only producer
 * op_new_iter/op_for_iter ever need to handle (see their own doc comments in
 * vm.c). Mirrors py-slang's own range() primitive contract exactly (see
 * builtins.ts case 30), including the arg-count range and the step == 0
 * rejection.
 */
static sinanbox_t sivmfn_prim_range(uint8_t argc, sinanbox_t *argv) {
  if (argc < 1 || argc > 3) {
    sifault(pynter_fault_function_arity);
    return NANBOX_OFEMPTY();
  }
  // NANBOX_ISNUMERIC + NANBOX_TOI32, not NANBOX_ISINT + NANBOX_INT: integers
  // outside the 21-bit small-int range are represented as floats in this
  // VM, so NANBOX_ISINT alone would wrongly reject an ordinary call like
  // range(2000000) with a type error. Matches how sivmfn_prim_gen_list/
  // sivmfn_prim_list_ref already accept either representation.
  for (uint8_t i = 0; i < argc; ++i) {
    if (!NANBOX_ISNUMERIC(argv[i])) {
      sifault(pynter_fault_type);
      return NANBOX_OFEMPTY();
    }
  }

  int32_t start, stop, step;
  if (argc == 1) {
    start = 0;
    stop = NANBOX_TOI32(argv[0]);
    step = 1;
  } else if (argc == 2) {
    start = NANBOX_TOI32(argv[0]);
    stop = NANBOX_TOI32(argv[1]);
    step = 1;
  } else {
    start = NANBOX_TOI32(argv[0]);
    stop = NANBOX_TOI32(argv[1]);
    step = NANBOX_TOI32(argv[2]);
  }

  if (step == 0) {
    sifault(pynter_fault_divide_by_zero);
    return NANBOX_OFEMPTY();
  }

  siheap_iterator_t *iter = siiterator_new(start, stop, step);
  return SIHEAP_PTRTONANBOX(iter);
}

/******************************************************************************
 * Miscellaneous primitives
 ******************************************************************************/

static sinanbox_t sivmfn_prim_display(uint8_t argc, sinanbox_t *argv) {
  SIDEBUG("Program called display with %d arguments:\n", argc);
  debug_display_argv(argc, argv);
  handle_display(argc, argv, false);
  if (argc) {
    siheap_refbox(argv[0]);
    return argv[0];
  }
  return NANBOX_OFUNDEF();
}

static inline bool structural_equal(sinanbox_t l, sinanbox_t r) {
  if (sivm_equal(l, r)) {
    return true;
  }

  if (!NANBOX_ISPTR(l) || !NANBOX_ISPTR(r)) {
    return false;
  }

  siheap_header_t *lv = SIHEAP_NANBOXTOPTR(l);
  siheap_header_t *rv = SIHEAP_NANBOXTOPTR(r);
  if (lv->type != sitype_array || rv->type != sitype_array) {
    return false;
  }

  siheap_array_t *la = (siheap_array_t *) lv;
  siheap_array_t *ra = (siheap_array_t *) rv;
  if (la->count != 2 || ra->count != 2) {
    return false;
  }

  return structural_equal(siarray_get(la, 0), siarray_get(ra, 0))
    && structural_equal(siarray_get(la, 1), siarray_get(ra, 1));
}

static sinanbox_t sivmfn_prim_equal(uint8_t argc, sinanbox_t *argv) {
  CHECK_ARGC(2);
  return NANBOX_OFBOOL(structural_equal(argv[0], argv[1]));
}

static sinanbox_t sivmfn_prim_error(uint8_t argc, sinanbox_t *argv) {
  SIDEBUG("Program called error with %d arguments:\n", argc);
  debug_display_argv(argc, argv);
  handle_display(argc, argv, true);
  sifault(pynter_fault_program_error);
  return NANBOX_OFEMPTY();
}

static sinanbox_t sivmfn_prim_unimpl(uint8_t argc, sinanbox_t *argv) {
  (void) argc; (void) argv;
  SIBUGV("Unimplemented primitive function %02x at address 0x%tx\n", *(sistate.pc + 1), SISTATE_CURADDR);
  sifault(pynter_fault_invalid_program);
  return NANBOX_OFEMPTY();
}

static sinanbox_t sivmfn_prim_noop(uint8_t argc, sinanbox_t *argv) {
  (void) argc; (void) argv;
  return NANBOX_OFUNDEF();
}

sivmfnptr_t sivmfn_primitives[] = {
  sivmfn_prim_accumulate,
  sivmfn_prim_append,
  sivmfn_prim_array_length,
  sivmfn_prim_build_list,
  sivmfn_prim_build_stream,
  sivmfn_prim_display,
  /* draw_data */ sivmfn_prim_noop, // not supported, obviously
  sivmfn_prim_enum_list,
  sivmfn_prim_enum_stream,
  sivmfn_prim_equal,
  sivmfn_prim_error,
  sivmfn_prim_eval_stream,
  sivmfn_prim_filter,
  sivmfn_prim_for_each,
  sivmfn_prim_head,
  sivmfn_prim_integers_from,
  sivmfn_prim_is_array,
  sivmfn_prim_is_boolean,
  sivmfn_prim_is_function,
  sivmfn_prim_is_list,
  sivmfn_prim_is_null,
  sivmfn_prim_is_number,
  sivmfn_prim_is_pair,
  sivmfn_prim_is_stream,
  sivmfn_prim_is_string,
  sivmfn_prim_is_undefined,
  sivmfn_prim_length,
  sivmfn_prim_list,
  sivmfn_prim_list_ref,
  sivmfn_prim_list_to_stream,
  /* list_to_string */ sivmfn_prim_unimpl, // do we want to implement this?
  sivmfn_prim_map,
  sivmfn_prim_math_abs,
  sivmfn_prim_math_acos,
  sivmfn_prim_math_acosh,
  sivmfn_prim_math_asin,
  sivmfn_prim_math_asinh,
  sivmfn_prim_math_atan,
  sivmfn_prim_math_atan2,
  sivmfn_prim_math_atanh,
  sivmfn_prim_math_cbrt,
  sivmfn_prim_math_ceil,
  sivmfn_prim_math_clz32,
  sivmfn_prim_math_cos,
  sivmfn_prim_math_cosh,
  sivmfn_prim_math_exp,
  sivmfn_prim_math_expm1,
  sivmfn_prim_math_floor,
  sivmfn_prim_math_fround,
  sivmfn_prim_math_hypot,
  sivmfn_prim_math_imul,
  sivmfn_prim_math_log,
  sivmfn_prim_math_log1p,
  sivmfn_prim_math_log2,
  sivmfn_prim_math_log10,
  sivmfn_prim_math_max,
  sivmfn_prim_math_min,
  sivmfn_prim_math_pow,
  sivmfn_prim_math_random,
  sivmfn_prim_math_round,
  sivmfn_prim_math_sign,
  sivmfn_prim_math_sin,
  sivmfn_prim_math_sinh,
  sivmfn_prim_math_sqrt,
  sivmfn_prim_math_tan,
  sivmfn_prim_math_tanh,
  sivmfn_prim_math_trunc,
  sivmfn_prim_member,
  sivmfn_prim_pair,
  /* parse_int */ sivmfn_prim_unimpl, // TODO: doesn't make sense without the ability to take input (prompt)
  sivmfn_prim_remove,
  sivmfn_prim_remove_all,
  sivmfn_prim_reverse,
  /* runtime */ sivmfn_prim_unimpl, // TODO: need to get time from host
  sivmfn_prim_set_head,
  sivmfn_prim_set_tail,
  sivmfn_prim_stream,
  sivmfn_prim_stream_append,
  sivmfn_prim_stream_filter,
  sivmfn_prim_stream_for_each,
  sivmfn_prim_stream_length,
  sivmfn_prim_stream_map,
  sivmfn_prim_stream_member,
  sivmfn_prim_stream_ref,
  sivmfn_prim_stream_remove,
  sivmfn_prim_stream_remove_all,
  sivmfn_prim_stream_reverse,
  sivmfn_prim_stream_tail,
  sivmfn_prim_stream_to_list,
  sivmfn_prim_tail,
  /* stringify */ sivmfn_prim_unimpl, // TODO: do we want this?
  /* prompt */ sivmfn_prim_unimpl, // TODO: need to call out to host
  // py-slang (Python frontend) additions, appended rather than inserted
  // alphabetically so none of the indices above shift.
  sivmfn_prim_is_integer,
  sivmfn_prim_is_float,
  sivmfn_prim_is_complex,
  sivmfn_prim_gen_list,
  sivmfn_prim_arity, // 96
  // 97-130: py-slang's PRIMITIVE_FUNCTIONS (builtins.ts) already assigns
  // these indices to real/imag/complex/parse/tokenize/
  // apply_in_underlying_python/various math_* functions/print_llist/input —
  // none implemented natively (browser/CSE-only, or, for print_llist,
  // simply not yet ported). py-slang's index for a name has never had to
  // match this array's length at the time the name was assigned (a
  // longstanding, known mismatch between the two projects' index tables —
  // see py-slang's README), so range() landing right after arity() at 97
  // would silently collide with "real" instead. Every index up to range()'s
  // real slot at 131 must stay an explicit, valid function pointer — the
  // same clean-faulting stub already used for actually-unimplemented-but-
  // Source-native primitives above (sivmfn_prim_unimpl) — rather than an
  // implicit zero-initialized gap, which would turn any of these (e.g.
  // print_llist, already exercised by py-slang's own native-Pynter test
  // suite and expected to fault cleanly) into a NULL function pointer call
  // instead of a controlled sifault().
  // 97-103: real/imag/complex/_concat_arrays(compiler-internal)/parse/
  // tokenize/apply_in_underlying_python — genuinely no native counterpart,
  // browser/CSE-only (see builtins.ts's PRIMITIVE_FUNCTIONS comment).
  sivmfn_prim_unimpl, sivmfn_prim_unimpl, sivmfn_prim_unimpl, sivmfn_prim_unimpl, // 97-100
  sivmfn_prim_unimpl, sivmfn_prim_unimpl, sivmfn_prim_unimpl, // 101-103
  // 104-125: the math_* functions below, now implemented natively (see
  // MATH_FN_DOMAIN/gcd/lcm/comb/factorial/isqrt/perm above) — previously
  // sivmfn_prim_unimpl stubs.
  sivmfn_prim_math_degrees, sivmfn_prim_math_erf, sivmfn_prim_math_erfc, sivmfn_prim_math_comb, // 104-107
  sivmfn_prim_math_factorial, sivmfn_prim_math_gcd, sivmfn_prim_math_isqrt, sivmfn_prim_math_lcm, // 108-111
  sivmfn_prim_math_perm, sivmfn_prim_math_fabs, sivmfn_prim_math_fma, sivmfn_prim_math_fmod, // 112-115
  sivmfn_prim_math_remainder, sivmfn_prim_math_copysign, sivmfn_prim_math_isfinite, sivmfn_prim_math_isinf, // 116-119
  sivmfn_prim_math_isnan, sivmfn_prim_math_ldexp, sivmfn_prim_math_exp2, sivmfn_prim_math_gamma, // 120-123
  sivmfn_prim_math_lgamma, sivmfn_prim_math_radians, // 124-125
  // 126-130: time_time/print_llist/math_nextafter/math_ulp/input — genuinely
  // unimplemented (no host clock/async stdin wiring, or, for print_llist,
  // simply not yet ported; math_nextafter/math_ulp are unimplemented on the
  // CSE side too, see math.ts).
  sivmfn_prim_unimpl, sivmfn_prim_unimpl, sivmfn_prim_unimpl, sivmfn_prim_unimpl, // 126-129
  sivmfn_prim_unimpl, // 130
  sivmfn_prim_range // 131
};
