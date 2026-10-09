#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$PROJECT_ROOT/vfs/minimal.zip" \
    --script "$PROJECT_ROOT/scripts/stage2/minimal.emu"
