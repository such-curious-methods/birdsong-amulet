#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export AMULET_ADVERTISEMENT_COMMAND='uuids 33dbba50-c8ae-4344-a905-3230af113634'
export AMULET_ADVERTISEMENT_LABEL='peer Amulet UUID'
export AMULET_ADVERTISEMENT_DESCRIPTION='Complete 128-bit service UUID: 33dbba50-c8ae-4344-a905-3230af113634'

exec "$script_dir/advertise-test-tracker.sh" "$@"
