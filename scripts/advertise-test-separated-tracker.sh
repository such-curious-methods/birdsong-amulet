#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir

# A synthetic Samsung SmartTag frame in the separated state (0x13), matching
# the provisional rule in tracker-signatures.org. The privacy ID is a fixed
# dummy, not a real tag's.
export AMULET_ADVERTISEMENT_COMMAND='service 0xfd5a 0x13 0x01 0x00 0x00 0xa5 0xa5 0xa5 0xa5 0xa5 0xa5 0xa5 0xa5 0x03'
export AMULET_ADVERTISEMENT_LABEL='synthetic separated SmartTag frame'
export AMULET_ADVERTISEMENT_DESCRIPTION='Service data 0xFD5A: 13 (separated) + aging counter + dummy privacy ID + properties. Expect a dim orange double blink after a few seconds, then at most once a minute.'

exec "$script_dir/advertise-test-tracker.sh" "$@"
