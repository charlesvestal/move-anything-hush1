#!/usr/bin/env bash
set -euo pipefail
rg '"id"\s*:\s*"hush1"' /Volumes/ExtFS/charlesvestal/github/move-everything-parent/move-anything/module-catalog.json

echo "PASS: module catalog has hush1 entry"
