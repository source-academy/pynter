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
destabilizing Sinter, which remains the fallback engine for the Source curriculum. As of this fork,
Pynter and its bytecode format, PVML, are unmodified copies of Sinter/SVML; the plan is for
[py-slang](https://github.com/source-academy/py-slang) to compile its Python variant (SICPy) to
PVML and run it on Pynter, diverging from SVML/Sinter only where Python's semantics actually need
it. We currently still follow the [Source VM specification](https://github.com/source-academy/js-slang/wiki/SVML-Specification)
as in the js-slang wiki (mirrored, and where PVML has started to diverge, updated, in the
[py-slang wiki](https://github.com/source-academy/py-slang/wiki), forked as
[PVML-Specification](https://github.com/source-academy/py-slang/wiki/PVML-Specification) and
[PVML-Instruction-Set](https://github.com/source-academy/py-slang/wiki/PVML-Instruction-Set)) and
use the [SVML reference compiler in js-slang](https://github.com/source-academy/js-slang/blob/master/src/vm/svmc.ts).

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
  `16777216 + 1 === 16777216`.
- Pynter does not support Python's complex numbers.
- The following primitives are not supported:
  - list_to_string
  - parse_int
  - runtime
  - prompt
  - stringify

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

Use the [SVML compiler CLI utility in js-slang](https://github.com/source-academy/js-slang/blob/master/src/vm/svmc.ts) to compile programs for testing — this still produces SVML, which is what PVML currently is byte-for-byte. (A real deployment of Pynter would integrate a compiler directly instead.)

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

For convenience, we have included a NPM package that exposes the CLI utility.

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

- `PYNTER_HEAP_SIZE`: size in bytes of the statically-allocated heap; defaults
  to `0x10000` i.e. 64 KB

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
