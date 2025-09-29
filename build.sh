#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
PLUG_DIR="$BUILD_DIR/plugins"
SRC_DIR="$ROOT_DIR/src"
OUT_DIR="$ROOT_DIR/output"

mkdir -p "$BUILD_DIR" "$PLUG_DIR" "$OUT_DIR"

OS="$(uname -s)"
CC=${CC:-cc}
CFLAGS="-O2 -std=c11 -Wall -Wextra -Wpedantic -pthread -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE"
LDFLAGS="-pthread"

dlflag=""
rpath=""
ext="so"
if [ "$OS" = "Darwin" ]; then
  ext="dylib"
  rpath="-Wl,-rpath,@loader_path/plugins"
  PLUGIN_LDFLAGS="-dynamiclib -undefined dynamic_lookup"
else
  dlflag="-ldl"
  rpath="-Wl,-rpath,\$ORIGIN/plugins"
  EXPORT_MAIN="-Wl,-E"
  PLUGIN_LDFLAGS="-shared"
fi

echo "Building core pipeline..."
$CC $CFLAGS -Isrc -Iplugins \
  "$SRC_DIR/bq.c" "$SRC_DIR/pipeline.c" \
  -o "$BUILD_DIR/pipeline" $LDFLAGS $dlflag $rpath ${EXPORT_MAIN:-}

echo "Building analyzer (spec main)..."
$CC $CFLAGS -Isrc -Iplugins \
  "$SRC_DIR/bq.c" "$SRC_DIR/main.c" \
  -o "$OUT_DIR/analyzer" $LDFLAGS $dlflag $rpath ${EXPORT_MAIN:-}

build_plugin() {
  name="$1"
  src="$SRC_DIR/plugins/$1.c"
  out="$PLUG_DIR/$1.$ext"
  echo "Building plugin $name -> $out"
  $CC $CFLAGS -Isrc -Iplugins $PLUGIN_LDFLAGS "$src" "$ROOT_DIR/plugins/plugin_common.c" -o "$out" $LDFLAGS ${dlflag:-}
}

build_plugin logger
build_plugin typewriter
build_plugin uppercaser
build_plugin rotator
build_plugin flipper
build_plugin expander
build_plugin sink_stdout

echo "Done. Run: $OUT_DIR/analyzer <queue_size> <plugins...>"

