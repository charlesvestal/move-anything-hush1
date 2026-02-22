#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"

[ -f "$repo_root/dist/hush1/module.json" ]
[ -f "$repo_root/dist/hush1/ui.js" ]
[ -f "$repo_root/dist/hush1/ui_chain.js" ]
[ -f "$repo_root/dist/hush1/dsp.so" ]

nm -D "$repo_root/dist/hush1/dsp.so" | rg "move_plugin_init_v2"

echo "PASS: module artifacts exist and symbol is exported"
