# @sourceacademy/pynter-wasm

A WebAssembly build of [Pynter](https://github.com/source-academy/pynter), the native VM that runs
[py-slang](https://github.com/source-academy/py-slang)'s PVML bytecode for Python (SICPy) §3. This
package lets a browser (or any JS runtime) run PVML programs directly, with no native binary to
build or ship — the same VM semantics as Pynter's native `runner`, compiled to WASM instead of a
native executable.

`pynterwasm.js`/`pynterwasm.wasm` are Emscripten build output (`MODULARIZE=1`,
`EXPORT_NAME=pynterwasm`); `pynterwasm.d.ts` is generated directly from that same build via
Emscripten's `--emit-tsd`, so the types always match the shipped binary exactly.

## Usage

```ts
import initPynter from "@sourceacademy/pynter-wasm";
// bundler-specific: however your build resolves .wasm imports
import wasmBinary from "@sourceacademy/pynter-wasm/pynterwasm.wasm";

const pynter = await initPynter({
  instantiateWasm(imports, callback) {
    return wasmBinary(imports).then(({ instance, module }) => {
      callback(instance, module);
      return instance.exports;
    });
  },
});

const alloc_heap = pynter.cwrap("siwasm_alloc_heap", null, ["number"]);
const alloc = pynter.cwrap("siwasm_alloc", "number", ["number"]);
const run = pynter.cwrap("siwasm_run", "number", ["number", "number"]);

alloc_heap(0x10000); // once, before any run() call
```

`run(ptr, size)` returns a pointer to an 8-byte `{ type: u32, value }` result struct — see
py-slang's own `pynter-wasm.ts` for a complete, worked example of allocating the input buffer,
calling `run`, and decoding the result.

### Fault reporting

On a fault (e.g. `1 / 0`, an unbounded-recursion stack overflow, a program calling `error()` — see
the [main README's fault table](../../../README.md#fault-codes) for the full list), the result
struct's `type` comes back `0` (`pynter_type_unknown`) — the struct itself never carries *why* the
run faulted, only that it didn't produce a value.

The fault name is only available as text: `siwasm_run` (`devices/wasm/wasm/lib.c`) `printf`s one
line to stdout at the end of every run, intended as a debugging aid for this package's browser demo,
not as a structured API:

- on success: `Program exited with result type <type>: <value>` — redundant with the result struct
  above; safe to ignore if you're already decoding that struct yourself.
- on a fault: `Program exited unsuccessfully: <fault name>` — the *only* place the fault name
  surfaces. If your embedding needs to report more than "the program faulted" (e.g. a REPL that
  wants to show `divide by zero` rather than a generic failure), capture this line via `Module.print`
  and parse it out, the same way py-slang's `PyPvmlPynterEvaluator` does:

```ts
const pynter = await initPynter({
  print(text: string) {
    const fault = /^Program exited unsuccessfully: (.+)$/.exec(text)?.[1];
    if (fault) {
      // e.g. surface `Pynter fault: divide by zero` instead of a bare
      // "type 0" decode failure once run()'s result struct comes back empty.
      lastFault = fault;
      return;
    }
    if (/^Program exited with result type /.test(text)) return; // redundant with the result struct
    handleProgramOutput(text);
  },
});
```

This intentionally doesn't require touching `lib.c` or rebuilding the WASM binary — Pynter's own
fault reporting is unchanged; only how a browser embedder chooses to surface it is.

## Versioning

Published from a GitHub Release on the main repo (`.github/workflows/publish-wasm-npm.yml`) — the
npm version always matches the release tag. Not published on every push to `master`; a release is
a deliberate, reviewed step, same as this repo's `runner` binary and py-slang's own pinned-commit
policy for consuming Pynter.
