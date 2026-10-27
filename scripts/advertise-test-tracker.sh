#!/usr/bin/env bash
set -euo pipefail

readonly controller="${1:-}"
readonly advertisement_command="${AMULET_ADVERTISEMENT_COMMAND:-manufacturer 0xffff 0x54 0x52 0x4b 0x52 0x01}"
readonly advertisement_label="${AMULET_ADVERTISEMENT_LABEL:-test tracker payload}"
readonly advertisement_description="${AMULET_ADVERTISEMENT_DESCRIPTION:-Manufacturer data: ff ff 54 52 4b 52 01 (0xffff + \"TRKR\" + version 1)}"
bluetoothctl_input=""
bluetoothctl_output=""
bluetoothctl_pid=""
cleanup_started=false

cleanup() {
    if "$cleanup_started"; then
        return
    fi
    cleanup_started=true

    printf '\nStopping the %s advertisement...\n' "$advertisement_label"
    if [[ -n "$bluetoothctl_input" ]]; then
        printf 'advertise off\nquit\n' >&"$bluetoothctl_input" 2>/dev/null || true
        exec {bluetoothctl_input}>&-
    fi
    if [[ -n "$bluetoothctl_pid" ]]; then
        for _ in {1..30}; do
            if ! kill -0 "$bluetoothctl_pid" 2>/dev/null; then
                break
            fi
            if [[ -n "$bluetoothctl_output" ]]; then
                IFS= read -r -t 0.1 _ <&"$bluetoothctl_output" || true
            else
                sleep 0.1
            fi
        done
        if kill -0 "$bluetoothctl_pid" 2>/dev/null; then
            kill "$bluetoothctl_pid" 2>/dev/null || true
        fi
        for _ in {1..10}; do
            if ! kill -0 "$bluetoothctl_pid" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        if kill -0 "$bluetoothctl_pid" 2>/dev/null; then
            kill -KILL "$bluetoothctl_pid" 2>/dev/null || true
        fi
        wait "$bluetoothctl_pid" 2>/dev/null || true
    fi
    if [[ -n "$bluetoothctl_output" ]]; then
        exec {bluetoothctl_output}<&-
    fi
}

if ! command -v bluetoothctl >/dev/null; then
    printf 'Required command is missing: bluetoothctl\n' >&2
    exit 1
fi

if ! systemctl is-active --quiet bluetooth.service; then
    printf 'bluetooth.service must be active before starting the test beacon.\n' >&2
    exit 1
fi

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

coproc BLUEZ_CLIENT { bluetoothctl; }
bluetoothctl_pid="$BLUEZ_CLIENT_PID"
bluetoothctl_input="${BLUEZ_CLIENT[1]}"
bluetoothctl_output="${BLUEZ_CLIENT[0]}"

if [[ -n "$controller" ]]; then
    printf 'select %s\n' "$controller" >&"$bluetoothctl_input"
fi
printf '%s\n' \
    'menu advertise' \
    'clear' \
    'rsi off' \
    "$advertisement_command" \
    'back' \
    'advertise broadcast' >&"$bluetoothctl_input"

advertisement_registered=false
registration_deadline=$((SECONDS + 10))
while ((SECONDS < registration_deadline)); do
    if IFS= read -r -t 1 output_line <&"$bluetoothctl_output"; then
        if [[ "$output_line" == *"Advertising object registered"* ]]; then
            advertisement_registered=true
            break
        fi
        if [[ "$output_line" == *"Invalid value"* ||
              "$output_line" == *"Invalid arguments"* ||
              "$output_line" == *"No default controller available"* ||
              "$output_line" == *"Controller "*" not available"* ||
              "$output_line" == *"Failed to register advertisement"* ]]; then
            printf '%s\n' "$output_line" >&2
            break
        fi
    elif ! kill -0 "$bluetoothctl_pid" 2>/dev/null; then
        break
    fi
done

if ! "$advertisement_registered"; then
    printf 'BlueZ did not register the test advertisement.\n' >&2
    exit 1
fi

printf 'Advertising %s. Press Ctrl-C to stop.\n' "$advertisement_label"
printf '%s\n' "$advertisement_description"
advertisement_error=""
while kill -0 "$bluetoothctl_pid" 2>/dev/null; do
    output_line=""
    IFS= read -r -t 1 output_line <&"$bluetoothctl_output" || true
    if [[ "$output_line" == *"Advertising released"* ||
          "$output_line" == *"Advertising object unregistered"* ||
          "$output_line" == *"Failed to"* ||
          "$output_line" == *"Waiting to connect to bluetoothd"* ]]; then
        advertisement_error="$output_line"
        break
    fi
done

if [[ -n "$advertisement_error" ]]; then
    printf 'Test advertisement stopped unexpectedly: %s\n' "$advertisement_error" >&2
else
    printf 'bluetoothctl exited while the test advertisement was active.\n' >&2
fi
exit 1
