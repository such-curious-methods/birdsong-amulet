#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$project_root"

assume_yes=false
list_only=false
board=firebeetle
ports=()
response_chord=""
response_delay=0
note_duration=1000
chord_call=false
call_delay=0
hunt_beacon=""
roles_file="$project_root/fleet-roles.csv"

usage() {
    cat <<'EOF'
Usage: ./scripts/flash-boards.sh [OPTIONS] [PORT ...]

Build the current firmware once for the selected board, then flash each
connected board sequentially.

Without PORT arguments, the script discovers adapters for the selected board:
  firebeetle  WCH CH340 bridges under /dev/serial/by-path. CH340s have no USB
              serial number, so by-id names collide when several are connected;
              by-path names identify the USB port instead.
  esp32c3     Silicon Labs CP210x adapters under /dev/serial/by-id.
Pass explicit ports to select other adapters or a subset.

Options:
  -b, --board NAME  Board profile: firebeetle (default) or esp32c3
  -l, --list        List selected ports without building or flashing
  -y, --yes         Skip the confirmation prompt
  -h, --help        Show this help

Chord tones (optional), given to each board after it is flashed:
  --response-chord ROOT  Give each board one tone of the major chord on ROOT
                         (for example C5) as its default Response: root,
                         third, fifth or octave. A board already listed in
                         the roles file keeps its role; a new board gets the
                         role with the fewest boards. Restore default on the
                         config page returns to the board's tone.
  --response-delay MS    Delay before answering another board's Call, 0-1000
                         in steps of 10 (default 0); signal-strength delay is
                         added on top
  --note-duration MS     Note length, 100-1000 in steps of 10 (default 1000)
  --chord-call           Give the board's Call the same tone; Restore default
                         returns to it too
  --call-delay MS        Delay before this board's own Call chime when its
                         button is pressed, with --chord-call (default 0);
                         does not affect responses
  --roles-file PATH      Record of each board's role (default fleet-roles.csv)

Scavenger hunt (optional, FireBeetle only):
  --hunt-beacon N        Flash a silent hunt beacon numbered N (1-8) instead
                         of an Amulet. It advertises continuously and glows a
                         slow rainbow. Built in build/firebeetle-hunt-N, so
                         the Amulet build is untouched

Examples:
  ./scripts/flash-boards.sh
  ./scripts/flash-boards.sh --list
  ./scripts/flash-boards.sh --board esp32c3
  ./scripts/flash-boards.sh --response-chord C5 --response-delay 200
  ./scripts/flash-boards.sh --response-chord C5 --chord-call
  ./scripts/flash-boards.sh --hunt-beacon 2
  ./scripts/flash-boards.sh /dev/serial/by-path/pci-...-usb-0:1:1.0-port0
EOF
}

while (($# > 0)); do
    case "$1" in
    -b|--board)
        if (($# < 2)); then
            printf 'Missing board name for %s\n\n' "$1" >&2
            usage >&2
            exit 2
        fi
        board="$2"
        shift
        ;;
    --board=*)
        board="${1#--board=}"
        ;;
    --chord-call)
        chord_call=true
        ;;
    --response-chord|--response-delay|--note-duration|--roles-file|--call-delay|--hunt-beacon)
        if (($# < 2)); then
            printf 'Missing value for %s\n\n' "$1" >&2
            usage >&2
            exit 2
        fi
        case "$1" in
        --response-chord) response_chord="$2" ;;
        --response-delay) response_delay="$2" ;;
        --note-duration) note_duration="$2" ;;
        --roles-file) roles_file="$2" ;;
        --call-delay) call_delay="$2" ;;
        --hunt-beacon) hunt_beacon="$2" ;;
        esac
        shift
        ;;
    -l|--list)
        list_only=true
        ;;
    -y|--yes)
        assume_yes=true
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    --)
        shift
        ports+=("$@")
        break
        ;;
    -*)
        printf 'Unknown option: %s\n\n' "$1" >&2
        usage >&2
        exit 2
        ;;
    *)
        ports+=("$1")
        ;;
    esac
    shift
done

if [[ -n "$hunt_beacon" ]]; then
    if [[ ! "$hunt_beacon" =~ ^[1-8]$ ]]; then
        printf -- '--hunt-beacon needs a number from 1 to 8.\n' >&2
        exit 2
    fi
    if [[ "$board" != firebeetle ]]; then
        printf -- '--hunt-beacon is only supported for --board firebeetle.\n' >&2
        exit 2
    fi
    if [[ -n "$response_chord" ]]; then
        printf -- '--hunt-beacon cannot be combined with --response-chord.\n' >&2
        exit 2
    fi
fi

case "$board" in
firebeetle)
    board_label='FireBeetle 2 ESP32-E'
    adapter_label='WCH CH340'
    build_dir=build/firebeetle
    defaults="$project_root/sdkconfig.firebeetle.defaults"
    if [[ -n "$hunt_beacon" ]]; then
        board_label="FireBeetle 2 ESP32-E (hunt beacon $hunt_beacon)"
        build_dir="build/firebeetle-hunt-$hunt_beacon"
        defaults="$defaults;$project_root/$build_dir/hunt-beacon.defaults"
    fi
    build_args=(
        -B "$build_dir"
        -D IDF_TARGET=esp32
        -D SDKCONFIG="$project_root/$build_dir/sdkconfig"
        -D SDKCONFIG_DEFAULTS="$defaults"
    )
    flash_args=(-B "$build_dir")
    ;;
esp32c3)
    board_label='ESP32-C3'
    adapter_label='Silicon Labs CP210x'
    build_args=()
    flash_args=()
    ;;
*)
    printf 'Unknown board: %s (expected firebeetle or esp32c3)\n' "$board" >&2
    exit 2
    ;;
esac

# Prints each by-path link whose USB vendor ID matches $1. The usbv2 aliases
# duplicate the plain usb links, so they are skipped.
discover_by_path_vendor() {
    local vendor_id="$1" link device
    shopt -s nullglob
    for link in /dev/serial/by-path/*; do
        [[ "$link" == *-usbv2-* ]] && continue
        device="$(readlink -f -- "$link")"
        # grep without -q reads all input, so udevadm never sees SIGPIPE,
        # which pipefail would otherwise report as a failed match.
        if udevadm info --query=property --name="$device" 2>/dev/null \
            | grep -x "ID_VENDOR_ID=$vendor_id" >/dev/null; then
            printf '%s\n' "$link"
        fi
    done
    shopt -u nullglob
}

if ((${#ports[@]} == 0)); then
    case "$board" in
    firebeetle)
        mapfile -t serial_devices < <(discover_by_path_vendor 1a86)
        ;;
    esp32c3)
        shopt -s nullglob
        serial_devices=(/dev/serial/by-id/usb-Silicon_Labs_CP210*)
        shopt -u nullglob
        ;;
    esac
    ports+=("${serial_devices[@]}")
fi

if ((${#ports[@]} == 0)); then
    printf '%s\n' \
        "No $adapter_label serial adapters were found." \
        'Connect the boards with data-capable USB cables, or pass their ports explicitly.' >&2
    exit 1
fi

# Deduplicate by the resolved device so two names for one board flash it once.
declare -A seen_devices=()
selected_ports=()
for port in "${ports[@]}"; do
    if [[ ! -e "$port" ]]; then
        printf 'Serial port does not exist: %s\n' "$port" >&2
        exit 1
    fi
    device="$(readlink -f -- "$port")"
    if [[ -z "${seen_devices[$device]+present}" ]]; then
        seen_devices[$device]=true
        selected_ports+=("$port")
    fi
done

printf 'Selected %d %s board(s):\n' "${#selected_ports[@]}" "$board_label"
for port in "${selected_ports[@]}"; do
    printf '  %s -> %s\n' "$port" "$(readlink -f -- "$port")"
done

if "$list_only"; then
    exit 0
fi

if ! "$assume_yes"; then
    if [[ ! -t 0 ]]; then
        printf 'Confirmation requires a terminal; rerun with --yes for unattended use.\n' >&2
        exit 1
    fi
    printf '\nBuild the current worktree and flash every selected port? [y/N] '
    confirmation=""
    read -r confirmation || true
    if [[ ! "$confirmation" =~ ^[Yy]([Ee][Ss])?$ ]]; then
        printf 'Flash cancelled.\n'
        exit 0
    fi
fi

if "$chord_call" && [[ -z "$response_chord" ]]; then
    printf -- '--chord-call needs --response-chord.\n' >&2
    exit 2
fi
chord_args=(--root "$response_chord" --delay "$response_delay" --duration "$note_duration")
if "$chord_call"; then
    chord_args+=(--call --call-delay "$call_delay")
fi
if [[ -n "$response_chord" ]] &&
    ! "$project_root/.venv/bin/python" "$project_root/scripts/provision-chord-tone.py" \
        --check "${chord_args[@]}"; then
    exit 2
fi

if [[ -n "$hunt_beacon" ]]; then
    mkdir -p -- "$project_root/$build_dir"
    printf 'CONFIG_AMULET_HUNT_BEACON_NUMBER=%s\n' "$hunt_beacon" \
        > "$project_root/$build_dir/hunt-beacon.defaults"
fi

printf '\nBuilding firmware once before flashing...\n'
if ! "$project_root/scripts/idf.sh" "${build_args[@]}" build; then
    printf 'Build failed; no boards were flashed.\n' >&2
    exit 1
fi

successful_ports=()
failed_ports=()
total_ports=${#selected_ports[@]}
flash_log="$(mktemp)"
trap 'rm -f -- "$flash_log"' EXIT

for index in "${!selected_ports[@]}"; do
    port="${selected_ports[$index]}"
    printf '\n[%d/%d] Flashing %s\n' "$((index + 1))" "$total_ports" "$port"
    if ! "$project_root/scripts/idf.sh" "${flash_args[@]}" -p "$port" flash 2>&1 | tee "$flash_log"; then
        printf 'Flash failed for %s; continuing with the remaining boards.\n' "$port" >&2
        failed_ports+=("$port")
        continue
    fi
    if [[ -n "$response_chord" ]]; then
        # esptool prints the chip's MAC while connecting; it keys the roles file.
        mac="$(grep -m1 -oE 'MAC: ([0-9a-f]{2}:){5}[0-9a-f]{2}' "$flash_log" | cut -d' ' -f2 || true)"
        if [[ -z "$mac" ]] ||
            ! "$project_root/.venv/bin/python" "$project_root/scripts/provision-chord-tone.py" \
                --port "$port" --mac "$mac" "${chord_args[@]}" \
                --roles-file "$roles_file"; then
            printf 'Chord tone not set for %s; continuing with the remaining boards.\n' "$port" >&2
            failed_ports+=("$port")
            continue
        fi
    fi
    successful_ports+=("$port")
done

printf '\nFlash summary: %d succeeded, %d failed.\n' \
    "${#successful_ports[@]}" "${#failed_ports[@]}"

if ((${#failed_ports[@]} > 0)); then
    printf 'Failed ports:\n' >&2
    printf '  %s\n' "${failed_ports[@]}" >&2
    exit 1
fi

printf 'All selected boards were flashed successfully.\n'
if [[ -n "$response_chord" ]]; then
    printf 'Chord-tone roles are recorded in %s.\n' "$roles_file"
fi
if [[ -n "$hunt_beacon" ]]; then
    printf 'Hunt beacon %s should now glow a slow rainbow; bring an Amulet close and watch for its rainbow.\n' "$hunt_beacon"
else
    printf 'Run ./scripts/advertise-test-tracker.sh and verify each board plays the red tracker arpeggio.\n'
fi
