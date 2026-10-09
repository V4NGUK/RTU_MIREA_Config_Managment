#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/nested.zip" \
    --script "$PROJECT_ROOT/scripts/stage3/full_demo.emu"
