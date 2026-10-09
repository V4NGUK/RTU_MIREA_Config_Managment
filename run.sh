#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")" && pwd)"
cmake -S "$project_root" -B "$project_root/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_root/build" --config Release
cmake -E chdir "$project_root/build" ctest --output-on-failure -C Release
exec "$project_root/build/emulator" --vfs "$project_root/vfs/minimal.zip"
