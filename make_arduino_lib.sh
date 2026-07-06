#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
TMPDIR="$(mktemp -d)"

mkdir -p "$TMPDIR/Pynter/src"
cp -rv "$DIR"/vm/src/* "$DIR"/vm/include/* "$TMPDIR/Pynter/src"
cp -v "$DIR/Arduino-library.properties" "$TMPDIR/Pynter/library.properties"
pushd "$TMPDIR"
zip -rv "$DIR/Pynter.zip" *
popd

rm -rf "$TMPDIR"
