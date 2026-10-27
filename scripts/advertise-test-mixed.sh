#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export AMULET_ADVERTISEMENT_COMMAND=$'manufacturer 0xffff 0x54 0x52 0x4b 0x52 0x01\nuuids 33dbba50-c8ae-4344-a905-3230af113634'
export AMULET_ADVERTISEMENT_LABEL='combined tracker and peer fixture'
export AMULET_ADVERTISEMENT_DESCRIPTION='Exact TRKR manufacturer data plus the Amulet 128-bit service UUID; tracker handling must take priority'

exec "$script_dir/advertise-test-tracker.sh" "$@"
