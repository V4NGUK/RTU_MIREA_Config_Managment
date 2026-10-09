#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ -n "${EMULATOR:-}" ]]; then
    EMULATOR_BIN="$EMULATOR"
else
    EMULATOR_BIN=""
    for candidate in \
        "$PROJECT_ROOT/build/emulator" \
        "$PROJECT_ROOT/build/emulator.exe" \
        "$PROJECT_ROOT/cmake-build-debug/emulator" \
        "$PROJECT_ROOT/cmake-build-debug/emulator.exe"; do
        if [[ -x "$candidate" ]]; then
            EMULATOR_BIN="$candidate"
            break
        fi
    done
fi

if [[ -z "$EMULATOR_BIN" || ! -x "$EMULATOR_BIN" ]]; then
    echo "Emulator executable not found. Build the project or set EMULATOR." >&2
    exit 2
fi

VFS_EXAMPLES="$(dirname "$EMULATOR_BIN")/vfs"
if [[ ! -d "$VFS_EXAMPLES" ]]; then
    VFS_EXAMPLES="$(dirname "$EMULATOR_BIN")/../vfs"
fi
export SHELL_EMULATOR_FIXTURES="$VFS_EXAMPLES"
mkdir -p "$PROJECT_ROOT/build/stage-output"
export SHELL_EMULATOR_OUTPUT="$PROJECT_ROOT/build/stage-output"
cd "$PROJECT_ROOT"
