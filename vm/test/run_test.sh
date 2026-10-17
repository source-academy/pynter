#!/bin/bash

set -o pipefail

runner="$1"
in_file="$2.pvm"
out_file="$2.out"
# Programs whose output depends on the NaN-box layout (e.g. an int that no
# longer fits a 21-bit small int, or a result rounded to a single precision
# float) have a second expected output, for the compact 32-bit layout.
if [ "$3" = "nanbox32" ] && [ -f "$2.out32" ]; then
  out_file="$2.out32"
fi

"$runner" "$in_file" | diff -u "$out_file" -
