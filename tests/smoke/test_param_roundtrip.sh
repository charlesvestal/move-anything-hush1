#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"

rg '"api_version"\s*:\s*2' "$repo_root/src/module.json"
rg '"component_type"\s*:\s*"sound_generator"' "$repo_root/src/module.json"
rg '"cutoff"' "$repo_root/src/module.json"
rg 'get_param' "$repo_root/src/dsp/sh101_plugin.c"

echo "PASS: parameter contract skeleton present"
