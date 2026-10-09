#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$PROJECT_ROOT/vfs/nested.zip" \
    --startup-script "$PROJECT_ROOT/scripts/stage2/nested.emu"
