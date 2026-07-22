# Pynter

[![Coverage Status](https://coveralls.io/repos/github/source-academy/pynter/badge.svg?branch=master)](https://coveralls.io/github/source-academy/pynter?branch=master)
[![npm](https://img.shields.io/npm/v/@sourceacademy/pynter-wasm.svg)](https://www.npmjs.com/package/@sourceacademy/pynter-wasm)

**Target: Python (SICPy) Chapter 3.** Pynter's goal is a native VM that correctly runs everything
py-slang's PVML compiler produces for Python §3 — not later chapters, and not the general
Source/JS semantics it inherited from Sinter. When a feature or bugfix decision is ambiguous, "does
this match Python §3?" is the question, not "does this match Source/Sinter?".

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
["Compiling your own programs"](#compiling-your-own-programs) below for what that means in practice.
We currently still follow the [Source VM specification](https://github.com/source-academy/js-slang/wiki/SVML-Specification)
as in the js-slang wiki as a baseline (mirrored, and where PVML has diverged, updated, in the
[py-slang wiki](https://github.com/source-academy/py-slang/wiki), forked as
[PVML-Specification](https://github.com/source-academy/py-slang/wiki/PVML-Specification) and
[PVML-Instruction-Set](https://github.com/source-academy/py-slang/wiki/PVML-Instruction-Set)).

For implementation details, see [here](vm/docs/impl.md).

## Directory layout

- `vm`: The actual VM library.
- `vm/test`: Some scripts to aid with CI testing.
- `runner`: A simple runner to run programs from the CLI.
- `test_programs`: PVML test programs that have been manually verified to be correct, as well as expected output for automated tests.
- `devices`: Some examples for using Pynter on various embedded platforms.

## Usage notes

Pynter implements most of Python (SICPy) §3, except:

- Numbers are single-precision floating points. This means that
  `16777216 + 1 === 16777216`. Complex numbers (below) are single-precision too — their `real`/`imag`
  components are each a 32-bit float, matching this VM's other numeric types, unlike the
  double-precision complex numbers py-slang's browser-pathway engines (CSE/PVML-in-browser/WASM) use.
- The following Python builtins compile successfully but fault at runtime if actually called,
  since their underlying native primitive is an unimplemented stub:
  - `str()`/`repr()` (native's own internal names for these primitive slots are "stringify"/
    "prompt" — vestiges of this project's Source/Sinter origins, not Python-facing names)
  - `time.time()`
  - `input()`
  - `print_llist()` (already tracked as a known gap in py-slang's own test suite, py-slang#259)

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
  if `PYNTER_STATIC_HEAP` is off); defaults to `0x39999a` i.e. 3.6 MiB. See
  ["Memory configuration"](#memory-configuration) below for why, and for the
  hard ceiling this can't exceed.

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

### Memory configuration

Pynter values are NaN-boxed into a single 32-bit word (`sinanbox_t`, see
`vm/include/pynter/nanbox.h`): a heap pointer's tag consumes the top 10 bits,
leaving only **22 bits** for the pointer payload itself. That payload isn't a
scaled index into some larger unit — `SIHEAP_PTRTONANBOX` (`vm/include/pynter/heap.h`)
encodes it as a **raw byte offset** from the heap's base address (`siheap`).
That makes

```
2^22 bytes = 0x400000 = 4 MiB
```

a hard ceiling: no Pynter heap object can ever live more than 4 MiB from the
start of the heap, on any target, because there is nowhere left in the
nanbox to store a larger offset. `PYNTER_HEAP_SIZE` cannot be raised beyond
`0x400000` — doing so would let the heap allocate objects whose offset can no
longer round-trip through a nanbox pointer.

The default, `0x39999a` (3.6 MiB), stays comfortably under that ceiling while
still using most of the address space NaN-boxing allows. Older versions of
this default were far smaller (`0x10000`, 64 KB) — a conservative choice better
suited to genuinely memory-constrained targets (see `devices/arduino`,
`devices/esp32`) than to targets with real RAM to spare (a desktop build, or
`devices/ev3`'s ev3dev-stretch Linux userspace), which were leaving almost
all of the 4 MiB NaN-boxing allows unused for no benefit.

If you're targeting a memory-constrained device, lower `PYNTER_HEAP_SIZE` to
fit — via the `PYNTER_HEAP_SIZE` CMake define, or by uncommenting and editing
the corresponding line in `vm/include/pynter_config.h` for non-CMake build
systems. `devices/wasm` instead sets `PYNTER_STATIC_HEAP=0` and has its JS/TS
host allocate and pass in the heap via `pynter_setup_heap` at whatever size
the embedding page chooses at runtime (see `devices/wasm/npm/README.md`),
rather than baking a fixed size into the binary at compile time.

### Numeric representation

Pynter's NaN-box gives `int` its own dedicated tag (`NANBOX_TINT`), distinct
from `float` — but that tag's payload is only **21 bits**, not the 32 you
might expect from an `int32_t`-sized operand:

```
NANBOX_INTMIN = -0x100000 = -1,048,576
NANBOX_INTMAX =  0x0FFFFF =  1,048,575
```

(see `vm/include/pynter/nanbox.h`). A value outside that range is silently
re-encoded as a `float` instead (`NANBOX_WRAP_INT`) rather than raising a
fault — so `type()` for the *same* Python source can differ purely based on
a literal's magnitude: `1048575` is a genuine `int`; `1048576` is a `float`.
py-slang's `PVMLCompiler` (`targetsPynter` mode) matches this exactly via its
own `PYNTER_INT_MIN`/`PYNTER_INT_MAX` constants when deciding between the
`LGCI` and `LGCF64` opcodes, so a value that would silently become a float at
the VM level is compiled as one openly, rather than tagged `LGCI` and then
quietly reinterpreted anyway (see py-slang's `pvml-compiler.ts` and
[pynter#6](https://github.com/source-academy/pynter/issues/6)).

This is a deliberate, narrow limit, not arbitrary-precision `int` support —
matching Pynter's embedded/32-bit-target design goals (see the top-level
project description). There is no plan to widen it without a demonstrated
need; the NaN-box does have unused address space that *could* fit a wider
tag if one ever becomes necessary (see `nanbox.h`'s own header comment for
the currently-unused ranges), but speculatively building that now isn't
justified by anything this VM actually needs to run today.

`float` itself is single-precision (`float32`, via `NANBOX_FLOAT`/
`NANBOX_OFFLOAT`), not `float64` — roughly 7 significant decimal digits, and
loses the ability to represent every integer *exactly* once a value's
magnitude exceeds `2^24` (16,777,216). This matters beyond ordinary
arithmetic: any `int` literal or computed value that falls outside the
21-bit range above becomes a `float`, so a large integer can lose exact
value, not just its `int` type, once it's large enough that float32 can't
represent it precisely (e.g. `2147483647` — `int32` max — prints as
`2147483648.0`, not because of an off-by-one bug, but because that's the
nearest value float32 can actually represent). Complex numbers
(`siheap_complex_t`) use the same `float32` components, for the same
embedded-target reason — see the complex-number section above.
