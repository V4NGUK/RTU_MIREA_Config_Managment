#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"
"$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/commands.zip" --script "$PROJECT_ROOT/scripts/stage4/full_demo.emu"
printf 'one\ntwo\n' | "$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/commands.zip" \
    --script "$PROJECT_ROOT/scripts/stage4/stdin_tac.emu"
printf 'hello\nworld\n' | "$EMULATOR_BIN" --vfs "$VFS_EXAMPLES/commands.zip" \
    --script "$PROJECT_ROOT/scripts/stage4/stdin_rev.emu"
