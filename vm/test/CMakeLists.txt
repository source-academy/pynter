cmake_minimum_required(VERSION 3.10)

project(vm_test C)

macro(add_run_test name)
  add_test(NAME "run_${name}" COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh" "${runner_BINARY_DIR}/runner" "${CMAKE_CURRENT_SOURCE_DIR}/../../test_programs/${name}")
endmacro()

macro(add_run_stderr_test name)
  add_test(NAME "run_${name}" COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/run_test_stderr.sh" "${runner_BINARY_DIR}/runner" "${CMAKE_CURRENT_SOURCE_DIR}/../../test_programs/${name}")
endmacro()

add_run_test(return_1)
add_run_test(multiply_21_and_2)
add_run_test(fact_recursive)
add_run_test(fact_iterative)
add_run_test(fact_iterative_34)
add_run_test(string_compare)
add_run_test(string_concat)
add_run_test(string_concat_compare)
add_run_test(fact_iterative_5000)
add_run_test(sum_iterative_10000000)
add_run_test(internal_function)
add_run_test(force_marksweep)
add_run_test(inf_minus_inf)

add_run_test(prim_is_function_stream)
if(NOT ${PYNTER_TEST_SHORT_DOUBLE})
  add_run_test(prim_math)
endif()
add_run_test(prim_math_lax)
add_run_test(prim_display_number)
add_run_test(prim_display_float)
add_run_test(prim_display_singletons)
add_run_test(prim_display_string)
add_run_test(prim_display_more)
add_run_test(prim_pair)
add_run_test(prim_list)
add_run_test(prim_set_pair)
add_run_test(prim_accumulate)
add_run_test(prim_enum_list)
add_run_test(prim_enum_stream)
add_run_test(prim_integers_from)

add_run_test(value_prim)
add_run_test(more_arithmetic)
add_run_test(no_uninitialised_load)

# The tests below are retired, not merely disabled: Pynter's mission (see the
# top-level README) is now specifically "correctly run everything py-slang's
# PVML compiler produces for Python §3" -- not general Source/JS semantics,
# and there is no current SVML-producing user of this VM to keep byte-for-byte
# compatible with. `index_array`/`resize_array`/`move_array`/`array_length`
# test Source's classic array auto-grow-on-out-of-range-assignment semantics
# specifically -- Python has no such thing (`xs[i] = v` past the end always
# raises IndexError, see py-slang issue #294/#299), so these can never pass
# under Python's strict list-assignment rules regardless of recompilation.
# The rest (`equals`, `prim_is_type`, `prim_pair_arrays`, `prim_list_arrays`,
# `prim_map`(_arrays/_primitive), `prim_equal`, `prim_set_pair_arrays`,
# `prim_length`, `prim_accumulate_arrays`, `prim_is_list`, `prim_append`,
# `prim_build_list`, `prim_filter`, `prim_for_each`, `prim_list_ref`,
# `prim_member`, `prim_remove`(_all), `prim_reverse`, `prim_stream`* family,
# `prim_is_stream`, `more_tail_calls`) all use raw array-literal syntax
# (`[a, b]`) in their `.js` source, which their pre-compiled `.pvm` binaries
# (built by the bundled, unmaintainable minified `tools/compiler/pvmc.js`)
# encode via `NEWA` with no operand -- Pynter's own `NEWA` now always expects
# a pre-sizing operand (see `op_new_a` in vm.c and py-slang's
# `pvml-compiler.ts`), so these stale binaries desync on decode. The
# `pair()`-primitive-based sibling of each (`prim_pair`, `prim_list`,
# `prim_set_pair`, `prim_accumulate`, kept above) never touches the
# user-facing `NEWA`/`STAG` bytecode at all and is unaffected. Equivalent
# functional coverage for all of this (map/filter/reduce/streams/pairs, all
# passing) now lives in py-slang's own multi-engine test suite instead (see
# that repo's `yarn pynter:report`), which doubles as this project's de-facto
# test framework going forward.

add_run_test(prim_display)
add_run_test(prim_error)
add_run_test(hello_world)
