# Pynter

[![Coverage Status](https://coveralls.io/repos/github/source-academy/pynter/badge.svg?branch=master)](https://coveralls.io/github/source-academy/pynter?branch=master)
[![npm](https://img.shields.io/npm/v/@sourceacademy/pynter-wasm.svg)](https://www.npmjs.com/package/@sourceacademy/pynter-wasm)

**Target: Python (SICPy) Chapter 3.** Pynter is a native VM that runs the bytecode py-slang's PVML
compiler produces for Python §3.

For implementation details, see [here](vm/docs/impl.md).

## Directory layout

- `vm`: The actual VM library.
- `vm/test`: Some scripts to aid with CI testing.
- `runner`: A simple runner to run programs from the CLI.
- `test_programs`: PVML test programs that have been manually verified to be correct, as well as expected output for automated tests.
- `devices`: Some examples for using Pynter on various embedded platforms.

## Usage notes

Pynter implements most of Python (SICPy) §3, except:

- Numbers are NaN-boxed in 64 bits by default, which makes `float`s double-precision (like Python's)
  and `int`s 32-bit: an integer that overflows 32 bits becomes a `float` (there are no
  arbitrary-precision integers yet, see [pynter#55](https://github.com/source-academy/pynter/issues/55)),
  so that `2147483647 + 1` is `2147483648.0`. Targets with a few KiB of RAM use a compact 32-bit
  layout (`PYNTER_NANBOX32`, selected automatically on AVR/Arduino) with single-precision floats and
  21-bit `int`s, where `16777216 + 1 === 16777216`. See ["Value layout"](#value-layout). Complex
  numbers (below) have the same precision as floats: their `real`/`imag` components are each a float.
- `time_time()` deliberately does **not** return real wall-clock/epoch time, even on devices that
  have one (native, EV3) — it returns seconds since approximately when the program started, as a
  monotonically increasing value. This follows from the single-precision point above, which still holds for
  the 32-bit layout: an
  epoch timestamp (~1.8e9 today) only has ~128 seconds of resolution at that magnitude in a 32-bit
  float, which would make it useless for the elapsed-time timing (sensor loops, delays) this exists
  for; keeping the value small keeps it precise. (EV3 also has no battery-backed RTC in the first
  place — ev3dev's own docs: "every time you disconnect the battery from the EV3, the clock is
  reset" — so real wall-clock time would be inaccurate there even before the precision problem.)
  Concretely: native/EV3/WASM measure from this primitive's first call in the process (`CLOCK_MONOTONIC`,
  not wall-clock, so a long-uptime machine's own boot time can't leak in as a large, imprecise
  number); ESP32/Arduino measure from device boot (`esp_timer_get_time()`/`millis()`), which is
  already close enough to "program start" on those, single-program targets. This means values are
  **not comparable across separate program runs** (unlike CPython's `time.time()`), and are only
  meaningful as a difference between two calls within the same run — see `sivmfn_prim_time_time`
  in `vm/src/primitives.c` for the full reasoning.
- `draw_data(value1, *values)` draws nothing (no Pynter device has a drawing canvas) and returns its
  first argument, like Source Academy's own. `breakpoint()`, `set_timeout(f, t)` and
  `clear_all_timeout()` are deliberate no-ops returning `None`: there is no debugger to stop in and no
  event loop to schedule a callback on, so `set_timeout`'s callback is never run. They still check
  their arity (`set_timeout` takes exactly 2 arguments, `clear_all_timeout` none).
- The following Python builtins compile successfully but fault at runtime if actually called,
  since their underlying native primitive is an unimplemented stub:
  - `input()` — not a missing feature so much as a genuinely hard one for this VM specifically:
    Pynter is a single-pass, synchronous C VM with no async/suspend-and-resume machinery, run
    across several different devices (a spawned subprocess, a WASM module in a browser tab, an
    ESP32, an Arduino, an EV3) through one shared primitive-dispatch table. "Read a byte from
    stdin" means something different on each of those (a Web Stream, a Unix pipe, a UART, nothing
    at all), and there's no uniform hook for that today — wiring it in a way that works
    consistently across every target is real, scoped work, not a quick primitive to fill in. (For
    context: py-slang's own browser-pathway CSE machine *does* support `input()`, by being
    `async function*` throughout so it can genuinely suspend on a host-supplied stream; its
    PVML-in-browser interpreter can't yet, for the same "no suspend point" reason as here — see
    [py-slang#237](https://github.com/source-academy/py-slang/issues/237).)

The full `math` module (including `comb`/`factorial`/`gcd`/`isqrt`/`lcm`/`perm`/`fabs`/`fma`/`fmod`/
`remainder`/`copysign`/`isfinite`/`isinf`/`isnan`/`ldexp`/`exp2`/`gamma`/`lgamma`/`radians`/`degrees`/
`erf`/`erfc`), Python list indexing/assignment (negative-index wraparound, strict bounds, `bool`/
`float` index rejection), and list multiplication (`list * int`/`int * list`, with correct
shallow-copy semantics for nested lists) are implemented natively and verified at 100% parity against
py-slang's own multi-engine test suite (`yarn pynter:report` in the
[py-slang](https://github.com/source-academy/py-slang) repo, with `PYNTER_RUNNER_PATH` pointed at a
local build of this project) — py-slang's test suite doubles as this project's own de-facto
functional test framework; see that repo's `src/tests/utils.ts` for how tests here are generated.
Domain-restricted math functions (`acos`, `acosh`, `asin`, `atanh`, `log`, `log1p`, `log2`,
`log10`, `sqrt`) raise a fault (`pynter_fault_value_error`, Python's `ValueError`) on out-of-domain
input, matching CPython, rather than silently returning NaN.

Python's complex numbers are supported too: literals, arithmetic (`+ - * / **`, unary negation),
`==`/`!=` (cross-type with `int`/`float`, per Python's numeric tower), `is`/`is not` (value-equality
between two complex values specifically — the one type in this dialect where `is` doesn't mean object
identity, matching py-slang's own CSE reference), `complex()`/`real()`/`imag()`/`is_complex()`, and a
complex-aware `abs()` (the modulus). Ordering comparisons (`< > <= >=`) correctly remain unsupported
for complex, matching CPython. The one gap: `complex("3+4j")`-style single-string-argument
construction isn't supported (this VM has no string-to-number parser) — the numeric argument forms
(0/1/2 args, `int`/`float`/`bool`/`complex`) all work.

The SICP-style `linked-list`/`stream` preludes (`map`/`filter`/`reduce`/`for_each`/`append`/`member`/
`remove`/`reverse`/`build_llist`/`enum_llist`/`llist_ref`, and the full `stream_*`/`build_stream`/
`enum_stream`/`integers_from`/`eval_stream` family) are plain Python code compiled like any other
program, so they work wherever their underlying primitives (`pair`/`head`/`tail`/`set_head`/
`set_tail`) do — fully supported and covered by the parity suite above. Note `map`/`filter`/`reduce`
only exist over linked lists (cons pairs) in this dialect, not over Python's native `list` type.

Usage recommendations:

- Treat arrays like C arrays, rather than JavaScript arrays (which are actually
  maps). Pynter does not (yet) have optimisations for sparse arrays. Python list subscript
  assignment (`xs[i] = v`) itself never auto-grows regardless — an out-of-range index always raises
  `IndexError`, matching CPython.

### Fault codes

`pynter_run`'s return value is a `pynter_fault_t` (see `vm/include/pynter.h`) — `pynter_fault_none`
(`0`) on a normal completion, one of the codes below otherwise. `PYNTER_DEBUG_LOGLEVEL=1` prints the
reason for most of these at the point they're raised (see ["CMake configuration"](#cmake-configuration)
below); every device-specific frontend (the CLI `runner`, the WASM build) additionally prints the
fault name as part of its own trailer line once the run ends — see e.g. `runner/src/runner.c` or
`devices/wasm/wasm/lib.c`'s `fault_names`.

| # | fault | when it fires | example Python that triggers it |
|---|---|---|---|
| 0 | `no fault` | normal completion | — |
| 1 | `out of memory` | the heap (see ["Memory configuration"](#memory-configuration)) is exhausted | `[0] * 10000000` |
| 2 | `type error` | an operator or primitive is applied to operand(s) of the wrong type; or an `if`/`elif`/`while` condition, or a conditional expression's (ternary's) test, is not a `bool` | `1 + "a"`<br>`if 1:`<br>`    pass` |
| 3 | `divide by zero` | `/`, `//`, or `%` with a zero divisor | `1 / 0` |
| 4 | `stack overflow` | the VM call stack is exhausted (e.g. unbounded recursion) | `def f(): return f()`<br>`f()` |
| 5 | `stack underflow` | the VM pops an empty value stack | not reachable from valid Python — a compiler or hand-crafted-bytecode bug |
| 6 | `uninitialised load` | reading a global/local/enclosing-scope slot that was declared but never assigned | `def f():`<br>`    global x`<br>`    return x`<br>`f()` |
| 7 | `invalid load` | loading from a nonexistent enclosing-scope slot | not reachable from valid Python — a compiler bug |
| 8 | `invalid program` | the bytecode itself is malformed (e.g. an unrecognised opcode) | not reachable from valid Python — a compiler bug or corrupted `.pvm` file |
| 9 | `internal error` | a VM invariant was violated | not reachable from valid Python — a VM bug |
| 10 | `incorrect function arity` | a function is called with the wrong number of arguments | `def f(a, b): pass`<br>`f(1)` |
| 11 | `program called error()` | the program calls Python's `error()` builtin | `error("asdf")` |
| 12 | `uninitialised heap` | a run was attempted before `pynter_setup_heap`/`siwasm_alloc_heap` | API misuse by the embedder, not reachable from Python |
| 13 | `stopped` | the host explicitly cancelled the run via `sistop()` (e.g. an external kill/interrupt hook) | not triggered from within the running program itself |
| 14 | `value error` | Python's `ValueError` — right type, but an out-of-domain value, for the domain-restricted `math` functions (`acos`, `acosh`, `asin`, `atanh`, `log`, `log1p`, `log2`, `log10`, `sqrt`) | `sqrt(-1)` |
| 15 | `index error` | Python's `IndexError` — a list subscript (read or write) outside `-len(list)..len(list)-1` | `[1, 2, 3][10]` |

On a fault, `pynter_run`'s `result` output parameter is left zeroed (`pynter_type_unknown`, i.e.
`0`) — there is no partial/last-known value to recover from it. The fault code above is the only
signal of *why* the run ended; a caller that wants to report anything more specific than "the
program faulted" needs to thread that code (or its name) through itself, the same way each
frontend's own trailer line does.

## Use it on a device

Pynter is a C library. For examples on how to use Pynter, see [the CLI
runner](runner/src/runner.c), [the Arduino sketch
example](devices/arduino/arduino.ino), or the [ESP32
example](devices/esp32/src/main.c). There is also a [WASM example](devices/wasm).

To create an Arduino library zip, run the script
[`make_arduino_lib.sh`](make_arduino_lib.sh). You can configure the Arduino
library by unzipping the zip and modifying
[`pynter_config.h`](include/pynter_config.h).

## Build locally

We use the CMake build system. Note: a compiler that supports C11 is _required_.
This excludes MSVC.

```
# cd into the root of the repository, then:
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
make test
runner/runner ../test_programs/hello_world.pvm
```

### Compiling your own programs

For Python (SICPy) programs — Pynter's actual target — use
[py-slang](https://github.com/source-academy/py-slang)'s own PVML compiler, via its `repl` CLI, to
compile and immediately run a file against a locally-built `runner`:

```
yarn repl <path to python file> --engine pvml --pynter <path to this repo's runner binary> -v 3
```

See py-slang's README (["Running the standalone CLI (repl)"](https://github.com/source-academy/py-slang#running-the-standalone-cli-repl))
for the full flag reference. Only Python §3 (`-v 3`) is supported, matching this project's own
target.

The [SVML compiler CLI utility in js-slang](https://github.com/source-academy/js-slang/blob/master/src/vm/svmc.ts)
(also bundled here as [`tools/compiler/pvmc.js`](tools/compiler/pvmc.js), used by the example below)
still works for simple Source (not Python) programs that don't build an array or list literal, but
**do not rely on it more generally** — since PVML's `NEWA` diverged from SVML's own encoding (see
above), any program compiled by it that constructs an array/list literal will desync Pynter's
bytecode decoding instead of running correctly, with no clean error to signal what went wrong.

Alternatively, you could also try the [web demo](https://source-academy.github.io/pynter/) (not yet deployed under this name), which uses Pynter compiled to WASM.

### Using Pynter's WASM build from npm

The same WASM build that powers the web demo is published as
[`@sourceacademy/pynter-wasm`](https://www.npmjs.com/package/@sourceacademy/pynter-wasm) — a
consumer (e.g. [py-slang](https://github.com/source-academy/py-slang), which depends on it directly
rather than vendoring a manually-copied build) can `npm install` it instead of building Emscripten
output locally by hand.

```
npm install @sourceacademy/pynter-wasm
```

`pynterwasm.js`/`pynterwasm.wasm` are the raw Emscripten build output
(`devices/wasm/wasm/CMakeLists.txt`, `MODULARIZE=1`/`EXPORT_NAME=pynterwasm`); `pynterwasm.d.ts` is
generated directly from that same build via Emscripten's `--emit-tsd`, so the shipped types always
match the shipped binary exactly — see [`devices/wasm/npm/README.md`](devices/wasm/npm/README.md)
for a usage example.

**Publishing/versioning**: released via [`.github/workflows/publish-wasm-npm.yml`](.github/workflows/publish-wasm-npm.yml),
triggered by a GitHub Release being published (never on a push to `master`) — the npm version
always matches the release tag. This is deliberate, not an oversight: a release is a reviewed,
human-decided step, matching this repo's own versioning philosophy for the native `runner` binary,
and py-slang's own pinned-commit (never floating) policy for consuming Pynter in the first place.
To publish a new version, cut a GitHub Release with the desired tag (e.g. `v0.3.0`) — the workflow
builds, packages, and publishes automatically from there, authenticating to npm via a repo-level
`NPM_TOKEN` secret (an automation/granular-access token with the 2FA-bypass option enabled, scoped
to this package — required since the publish step runs unattended in CI with no human present to
supply a one-time code).

For convenience, we have included a NPM package that exposes the CLI utility. Note this compiles
plain Source (`.js`), not Python — see ["Compiling your own programs"](#compiling-your-own-programs)
above for why it's no longer reliable for anything beyond simple programs with no array/list literal.

Try it out:

```
# Make sure you have built the test runner from above.
# Then, from the root of the repository:
cd tools/compiler
yarn install
echo 'display("Hello world");' > myprogram.js
node pvmc.js myprogram.js
../../build/runner/runner myprogram.pvm
# (or wherever the test runner binary is)
```

### CMake configuration

Some configuration is available via CMake defines:

- `CMAKE_BUILD_TYPE`: controls the build type; defaults to `Debug`

  - `Debug`: assertions are enabled; some extra checks are enabled; `-Og` optimisation level
  - `Release`: assertions are disabled; `-O2` optimisation level

- `PYNTER_STATIC_HEAP`: if `1`, the heap is a fixed-size static array baked
  into the binary rather than allocated by the host program via
  `pynter_setup_heap`. Defaults to `1` for CMake-based builds (this repo's
  `vm/CMakeLists.txt` sets it as a cache variable) — but defaults to *off* if
  you're setting defines directly in `pynter_config.h` instead of via CMake
  (e.g. Arduino), since that header has no CMake cache variable to inherit
  from. See ["Memory configuration"](#memory-configuration) below.

- `PYNTER_HEAP_SIZE`: size in bytes of the statically-allocated heap (ignored
  if `PYNTER_STATIC_HEAP` is off); defaults to `0x399998` i.e. 3.6 MiB, and has
  to be a multiple of 8. See
  ["Memory configuration"](#memory-configuration) below for why, and for the
  hard ceiling this can't exceed with `PYNTER_NANBOX32`.

- `PYNTER_NANBOX32`: if `1`, uses the compact 32-bit NaN-box (single-precision
  floats, 21-bit `int`s, a heap of at most 4 MiB) instead of the default 64-bit
  one. Defaults to unset, except on AVR. See ["Value layout"](#value-layout).

- `PYNTER_STACK_ENTRIES`: size in stack entries of the statically-allocated
  stack; defaults to `0x200` i.e. 512

- `PYNTER_DISABLE_CHECKS`: if `1`, disables certain safety checks in the runtime
  e.g. stack over/underflow checks; defaults to unset (i.e. safety checks are
  performed)

- `PYNTER_DEBUG_LOGLEVEL`: controls the debug output level; defaults to `0`

  - `0`: all debug output is disabled.
  - `1`: prints reasons for most faults/errors
  - `2`: traces every instruction executed and every push on/pop off the stack

  This is available regardless of `CMAKE_BUILD_TYPE`.

  When deploying on an actual microcontroller, you will likely want to use `0`.
  `1` and `2` requires some implementation of `fprintf` and `stderr`. (This may
  be relaxed in future so the library consumer can provide a logging function
  instead.)

- `PYNTER_DEBUG_ABORT_ON_FAULT`: if `1`, raises an assertion failure when a
  fault occurs. (Intended for use when debugging under e.g. GDB.) Defaults to
  unset.

- `PYNTER_DEBUG_MEMORY_CHECK`: if `1`, does _a lot_ of checks at every
  instruction to verify the correctness of the heap linked list, freelist,
  stack, and reference counting. Note: this slows down execution severely.
  Defaults to unset.

### Value layout

Pynter values are NaN-boxed (`sinanbox_t`, see `vm/include/pynter/nanbox.h`). There are two layouts,
selected when building:

|  | 64-bit (default) | 32-bit (`PYNTER_NANBOX32`) |
| --- | --- | --- |
| size of a value | 8 bytes | 4 bytes |
| `float` | double precision (`f64`, like Python's) | single precision (`f32`) |
| small `int` | 32 bits: `-2147483648` to `2147483647` | 21 bits: `-1048576` to `1048575` |
| heap offset | 32 bits (heap up to 4 GiB) | 22 bits (heap up to 4 MiB) |
| complex number components | `f64` | `f32` |

The 32-bit layout is meant for targets with a few KiB of RAM, where a value has to fit in 4 bytes,
and is selected automatically on AVR (Arduino). Every other target, including the browser (WASM),
EV3 and ESP32, uses the 64-bit layout by default. (The ESP32's FPU is single precision only, so
`f64` arithmetic is slower there; a build for the ESP32 that favors speed over precision can
define `PYNTER_NANBOX32`. On the EV3's FPU-less ARM9, `f32` and `f64` are both emulated in software,
and `f64` costs about 2× on float arithmetic only.) CMake builds select the compact layout with
`-DPYNTER_NANBOX32=1`; others by uncommenting the define in `vm/include/pynter_config.h` or passing
`-DPYNTER_NANBOX32`.

Both layouts run the same bytecode (constants are `f64` in the bytecode either way, and are
narrowed on load in the 32-bit layout), and both are tested in CI. The C API differs: `pynter_value_t`
holds a `sifloat_t` (a `double` or a `float`) and an `object_value` of 64 or 32 bits, so with the
64-bit layout its union starts at byte offset 8 instead of 4 (16 bytes in total on wasm32).

### Memory configuration

A heap pointer in a NaN-box is a **raw byte offset** from the heap's base address (`siheap`) —
`SIHEAP_PTRTONANBOX` (`vm/include/pynter/heap.h`) encodes it so. With the 64-bit layout the offset is
32 bits wide, so the heap can be as large as 4 GiB; with `PYNTER_NANBOX32` it is 22 bits wide, and

```
2^22 bytes = 0x400000 = 4 MiB
```

is a hard ceiling: no Pynter heap object can ever live more than 4 MiB from the start of the heap,
because there is nowhere left in the NaN-box to store a larger offset. `PYNTER_HEAP_SIZE` cannot be
raised beyond `0x400000` there — doing so would let the heap allocate objects whose offset can no
longer round-trip through a NaN-box pointer.

All heap blocks are 8-byte aligned (with the 64-bit layout) so that values inside heap objects are
aligned: some CPUs, like the EV3's ARM9, fault on unaligned 8-byte accesses. So `PYNTER_HEAP_SIZE` has to be a multiple of 8, and a
heap given to `pynter_setup_heap` is aligned and rounded down to fit.

The default, `0x399998` (3.6 MiB), stays comfortably under the 4 MiB ceiling of the 32-bit layout
while still using most of the address space it allows. Older versions of
this default were far smaller (`0x10000`, 64 KB) — a conservative choice better
suited to genuinely memory-constrained targets (see `devices/arduino`,
`devices/esp32`) than to targets with real RAM to spare (a desktop build, or
`devices/ev3`'s ev3dev-stretch Linux userspace), which were leaving almost
all of the 4 MiB unused for no benefit. With the 64-bit layout, values take twice the space, so that
heap holds half as many of them; targets with RAM to spare can raise `PYNTER_HEAP_SIZE`.

If you're targeting a memory-constrained device, lower `PYNTER_HEAP_SIZE` to
fit — via the `PYNTER_HEAP_SIZE` CMake define, or by uncommenting and editing
the corresponding line in `vm/include/pynter_config.h` for non-CMake build
systems. `devices/wasm` instead sets `PYNTER_STATIC_HEAP=0` and has its JS/TS
host allocate and pass in the heap via `pynter_setup_heap` at whatever size
the embedding page chooses at runtime (see `devices/wasm/npm/README.md`),
rather than baking a fixed size into the binary at compile time.

### Numeric representation

Pynter's NaN-box gives `int` its own dedicated tag (`NANBOX_TINT`), distinct
from `float`. With the 64-bit layout that tag's payload is 32 bits wide:

```
NANBOX_INTMIN = -0x80000000 = -2,147,483,648
NANBOX_INTMAX =  0x7FFFFFFF =  2,147,483,647
```

With `PYNTER_NANBOX32` it is only **21 bits**:

```
NANBOX_INTMIN = -0x100000 = -1,048,576
NANBOX_INTMAX =  0x0FFFFF =  1,048,575
```

(see `vm/include/pynter/nanbox.h`). A value outside that range is silently
re-encoded as a `float` instead (`NANBOX_WRAP_INT`) rather than raising a
fault — so `type()` for the *same* Python source can differ purely based on
a literal's magnitude: `2147483647` is a genuine `int`; `2147483648` is a `float` (and `1048576`,
with `PYNTER_NANBOX32`).

This is a deliberate, narrow limit, not arbitrary-precision `int` support (that is tracked in
[pynter#55](https://github.com/source-academy/pynter/issues/55)). A float is exact for every
integer up to `2^53` with the 64-bit layout, so integers beyond the `int` range stay exact for a
while (e.g. `2147483647 + 1` is exactly `2147483648.0`), unlike with the 32-bit layout, where a float
is exact only up to `2^24` (16,777,216) and so large integers lose their exact value (e.g.
`2147483647` prints as `2147483648.0`, not because of an off-by-one bug, but because that's the
nearest value a `float32` can actually represent).

py-slang's `PVMLCompiler` (`targetsPynter` mode) decides between `LGCI` and `LGCF64` for a literal
using the full 32-bit signed range (`I32_MIN`/`I32_MAX` in `pvml-compiler.ts`), which is exactly the
range of the 64-bit layout. With `PYNTER_NANBOX32`, a literal like `1048576` (well within 32 bits)
still compiles to `LGCI`, is serialised as a full 32-bit operand, and only gets silently
reinterpreted as a `float` at the VM level when loaded (via `NANBOX_WRAP_INT`, as above). See
[pynter#6](https://github.com/source-academy/pynter/issues/6) for the original report.

`float` is a `sifloat_t`: double precision (`double`) with the 64-bit layout, like Python's, and
single precision (`float`) with `PYNTER_NANBOX32`, which gives roughly 7 significant decimal digits.
Complex numbers (`siheap_complex_t`) use the same type for their components — see the complex-number
section above.

## Origins

Name etymology: portmanteau of <strong>Py</strong>thon and Si<strong>nter</strong>.

Pynter is a fork of [Sinter](https://github.com/source-academy/sinter) — an implementation of the
Source Virtual Machine Language (SVML) intended for microcontroller platforms like an Arduino — kept
as a separate sister project so that giving the VM Python-specific semantics doesn't risk
destabilizing Sinter, which remains the fallback engine for the Source curriculum. Pynter and its
bytecode format, PVML, *started* as unmodified copies of Sinter/SVML, with
[py-slang](https://github.com/source-academy/py-slang) compiling its Python variant (SICPy) to PVML
and running it on Pynter — the plan was always to diverge from SVML/Sinter only where Python's
semantics actually need it, and that's since begun happening for real: `NEWA` (array/list-literal
construction) now takes a size operand pynter pre-sizes the backing array to, which the original
SVML encoding never had, needed so Python's strict (non-auto-growing) list subscript-assignment
rules can be enforced correctly (see [py-slang issue #299](https://github.com/source-academy/py-slang/issues/299)).
Do not assume PVML is byte-for-byte identical to SVML anymore, even though most of it still is — see
["Compiling your own programs"](#compiling-your-own-programs) above for what that means in practice.
PVML's [Specification](https://github.com/source-academy/py-slang/wiki/PVML-Specification) and
[Instruction-Set](https://github.com/source-academy/py-slang/wiki/PVML-Instruction-Set) pages, in the
[py-slang wiki](https://github.com/source-academy/py-slang/wiki), were each forked once from the
corresponding [SVML-Specification](https://github.com/source-academy/js-slang/wiki/SVML-Specification)/SVML-Instruction-Set
page in the js-slang wiki as a starting point, and have been maintained independently from there as
PVML has diverged.
