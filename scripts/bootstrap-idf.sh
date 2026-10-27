#!/usr/bin/env bash
set -euo pipefail

readonly idf_version="v5.5.5"
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
idf_path="$project_root/.tools/esp-idf"
python_version="$(<"$project_root/.python-version")"

for command_name in git uv; do
    if ! command -v "$command_name" >/dev/null; then
        printf 'Required command is missing: %s\n' "$command_name" >&2
        exit 1
    fi
done

uv python install "$python_version"
mkdir -p "$project_root/.tools" "$project_root/.venv"

if [[ ! -d "$idf_path/.git" ]]; then
    git clone \
        --branch "$idf_version" \
        --depth 1 \
        --recursive \
        --shallow-submodules \
        https://github.com/espressif/esp-idf.git \
        "$idf_path"
fi

installed_version="$(git -C "$idf_path" describe --tags --exact-match 2>/dev/null || true)"
if [[ "$installed_version" != "$idf_version" ]]; then
    printf 'Expected ESP-IDF %s, but %s contains %s.\n' \
        "$idf_version" "$idf_path" "${installed_version:-an untagged revision}" >&2
    exit 1
fi

git -C "$idf_path" submodule update --init --recursive --depth 1
if [[ -n "$(git -C "$idf_path" status --short --untracked-files=no)" ]]; then
    printf 'ESP-IDF checkout contains modified or missing tracked files: %s\n' "$idf_path" >&2
    exit 1
fi

python_path="$(uv python find "$python_version")"
export PATH="$(dirname "$python_path"):$PATH"
export IDF_TOOLS_PATH="$project_root/.idf-tools"
export IDF_PYTHON_ENV_PATH="$project_root/.venv"

"$idf_path/install.sh" esp32c3,esp32
