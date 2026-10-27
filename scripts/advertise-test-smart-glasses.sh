#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir

# Manufacturer data under Meta Platforms' Bluetooth company ID, the vendor-tier
# smart-glasses signal in tracker-signatures.org.
export AMULET_ADVERTISEMENT_COMMAND='manufacturer 0x01ab 0x00 0x00'
export AMULET_ADVERTISEMENT_LABEL='synthetic smart-glasses maker frame'
export AMULET_ADVERTISEMENT_DESCRIPTION='Manufacturer data: ab 01 (Meta Platforms) + 00 00. Expect a dim violet double blink after a few seconds, then at most once a minute.'

exec "$script_dir/advertise-test-tracker.sh" "$@"
