This directory contains a script that simply executes the
[SVML compiler CLI utility in js-slang](https://github.com/source-academy/js-slang/blob/master/src/vm/svmc.ts).

This still compiles Source to SVML, not SICPy to PVML: PVML is currently identical to SVML (see
the top-level README), so this is also how you produce `.pvm` test fixtures for now. Once
py-slang's own PVML compiler diverges from SVML, this should point at py-slang's compiler instead
— see [py-slang's `docs/pvml/`](https://github.com/source-academy/py-slang/tree/main/docs/pvml)
(forked from the SVML wiki docs) for where that format is documented.
