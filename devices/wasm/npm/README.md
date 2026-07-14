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

## Versioning

Published from a GitHub Release on the main repo (`.github/workflows/publish-wasm-npm.yml`) — the
npm version always matches the release tag. Not published on every push to `master`; a release is
a deliberate, reviewed step, same as this repo's `runner` binary and py-slang's own pinned-commit
policy for consuming Pynter.
