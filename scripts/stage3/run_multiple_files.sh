#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/multiple-files.zip" \
    --script "$PROJECT_ROOT/scripts/stage2/multiple-files.emu"
