#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d dist/hush1 ]; then
  echo "dist/hush1 missing; run ./scripts/build.sh first"
  exit 1
fi

TARGET="/data/UserData/move-anything/modules/sound_generators/hush1"
ssh ableton@move.local "mkdir -p $TARGET"
scp -r dist/hush1/* ableton@move.local:"$TARGET/"
ssh ableton@move.local "chmod -R a+rw $TARGET"

echo "Installed HUSH ONE module to $TARGET"
