#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
idf_path="$project_root/.tools/esp-idf"

if [[ ! -x "$idf_path/tools/idf.py" ]]; then
    printf 'ESP-IDF is missing. See README.md for local setup instructions.\n' >&2
    exit 1
fi

python_version="$(<"$project_root/.python-version")"
python_path="$(uv python find "$python_version")"

export PATH="$(dirname "$python_path"):$PATH"
export IDF_TOOLS_PATH="$project_root/.idf-tools"
export IDF_PYTHON_ENV_PATH="$project_root/.venv"

source "$idf_path/export.sh" >/dev/null
exec idf.py "$@"
