#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export AMULET_ADVERTISEMENT_COMMAND='manufacturer 0x004c 0x02 0x15 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f 0x12 0x34 0xab 0xcd 0xc5'
export AMULET_ADVERTISEMENT_LABEL='iBeacon-format location marker'
export AMULET_ADVERTISEMENT_DESCRIPTION='Manufacturer data: 4c 00 02 15 + UUID 00010203-0405-0607-0809-0a0b0c0d0e0f + major 0x1234 + minor 0xabcd + calibration -59 dBm'

exec "$script_dir/advertise-test-tracker.sh" "$@"
