#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs-path "$VFS_EXAMPLES/multiple-files.zip" \
    --startup-script "$PROJECT_ROOT/scripts/stage2/multiple-files.emu"
