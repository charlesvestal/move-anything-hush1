#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="move-anything-builder"

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
  if command -v docker >/dev/null 2>&1; then
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
      docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
      -v "$REPO_ROOT:/build" \
      -u "$(id -u):$(id -g)" \
      -w /build \
      "$IMAGE_NAME" \
      ./scripts/build.sh
    exit 0
  fi
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
if ! command -v "${CROSS_PREFIX}gcc" >/dev/null 2>&1; then
  echo "Compiler ${CROSS_PREFIX}gcc not found; falling back to host gcc"
  CROSS_PREFIX=""
fi

cd "$REPO_ROOT"
mkdir -p build dist/hush1

"${CROSS_PREFIX}gcc" -std=c11 -O3 -g -shared -fPIC \
  src/dsp/sh101_plugin.c \
  src/dsp/sh101_control.c \
  src/dsp/sh101_osc.c \
  src/dsp/sh101_env.c \
  src/dsp/sh101_filter.c \
  src/dsp/sh101_lfo.c \
  -o build/dsp.so \
  -Isrc \
  -Isrc/dsp \
  -lm

cat src/module.json > dist/hush1/module.json
cat src/ui.js > dist/hush1/ui.js
cat build/dsp.so > dist/hush1/dsp.so

(
  cd dist
  tar -czf hush1-module.tar.gz hush1
)

echo "Build complete: dist/hush1 and dist/hush1-module.tar.gz"
