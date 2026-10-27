#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir
readonly beacon="${AMULET_HUNT_BEACON_NUMBER:-1}"

if [[ ! "$beacon" =~ ^[1-8]$ ]]; then
    printf 'AMULET_HUNT_BEACON_NUMBER must be from 1 to 8.\n' >&2
    exit 2
fi

export AMULET_ADVERTISEMENT_COMMAND="manufacturer 0xffff 0xa3 0x01 0x0$beacon"
export AMULET_ADVERTISEMENT_LABEL="scavenger hunt beacon $beacon"
export AMULET_ADVERTISEMENT_DESCRIPTION="Manufacturer data: ff ff a3 01 0$beacon (0xffff + type 0xa3 + version 1 + beacon $beacon)"

exec "$script_dir/advertise-test-tracker.sh" "$@"
