#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
bash "$script_dir/run_minimal.sh"
bash "$script_dir/run_multiple_files.sh"
bash "$script_dir/run_nested.sh"
