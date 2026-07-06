# Pynter

[![Coverage Status](https://coveralls.io/repos/github/source-academy/pynter/badge.svg?branch=master)](https://coveralls.io/github/source-academy/pynter?branch=master)

Name etymology: portmanteau of <strong>Py</strong>thon and Si<strong>nter</strong>.

Pynter is a fork of [Sinter](https://github.com/source-academy/sinter) — an implementation of the
Source Virtual Machine Language (SVML) intended for microcontroller platforms like an Arduino — kept
as a separate sister project so that giving the VM Python-specific semantics doesn't risk
destabilizing Sinter, which remains the fallback engine for the Source curriculum. As of this fork,
Pynter and its bytecode format, PVML, are unmodified copies of Sinter/SVML; the plan is for
[py-slang](https://github.com/source-academy/py-slang) to compile its Python variant (SICPy) to
PVML and run it on Pynter, diverging from SVML/Sinter only where Python's semantics actually need
it. We currently still follow the [Source VM specification](https://github.com/source-academy/js-slang/wiki/SVML-Specification)
as in the js-slang wiki (mirrored, and where PVML has started to diverge, updated, in
[py-slang's `docs/pvml/`](https://github.com/source-academy/py-slang/tree/main/docs/pvml)) and use
the [SVML reference compiler in js-slang](https://github.com/source-academy/js-slang/blob/master/src/vm/svmc.ts).

For implementation details, see [here](vm/docs/impl.md).

## Directory layout

- `vm`: The actual VM library.
- `vm/test`: Some scripts to aid with CI testing.
- `runner`: A simple runner to run programs from the CLI.
- `test_programs`: PVML test programs that have been manually verified to be correct, as well as expected output for automated tests.
- `devices`: Some examples for using Pynter on various embedded platforms.

## Usage notes

Pynter implements most of Source §3, except:

- Numbers are single-precision floating points. This means that
  `16777216 + 1 === 16777216`.
- The following primitives are not supported:
  - list_to_string
  - parse_int
  - runtime
  - prompt
  - stringify

Usage recommendations:

- Treat arrays like C arrays, rather than JavaScript arrays (which are actually
  maps). Pynter does not (yet) have optimisations for sparse arrays.

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
