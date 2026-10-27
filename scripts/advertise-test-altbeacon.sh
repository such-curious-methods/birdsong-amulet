#!/usr/bin/env bash
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

export AMULET_ADVERTISEMENT_COMMAND='manufacturer 0x0118 0xbe 0xac 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f 0x10 0x11 0x12 0x13 0xc5 0x00'
export AMULET_ADVERTISEMENT_LABEL='AltBeacon v1.0 location marker'
export AMULET_ADVERTISEMENT_DESCRIPTION='Manufacturer data: company 0x0118 + code be ac + Beacon ID 000102030405060708090a0b0c0d0e0f10111213 + reference RSSI -59 dBm + reserved 00'

exec "$script_dir/advertise-test-tracker.sh" "$@"
