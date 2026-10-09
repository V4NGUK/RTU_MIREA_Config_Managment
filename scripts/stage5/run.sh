#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/commands.zip" --script "$PROJECT_ROOT/scripts/stage5/full_demo.emu"
