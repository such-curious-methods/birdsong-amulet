#!/usr/bin/env python3
"""Give one flashed Amulet a chord tone as its default Response (and Call).

The fleet splits into four roles of a major chord: root, third, fifth and
octave. A board already listed in the roles file keeps its role; a new board
gets whichever role has the fewest boards, so the quarters stay even across
flashing batches. The tone is stored with the firmware's DEFAULT command, so
the config page's Restore default returns to it.

Usage: provision-chord-tone.py --port PORT --mac MAC --root C5
       --roles-file PATH [--delay MS] [--duration MS] [--call [--call-delay MS]]
       provision-chord-tone.py --check --root C5 [--delay MS] [--duration MS]
"""

import argparse
import csv
import datetime
import re
import sys
import time
from pathlib import Path

import serial

PREFIX = "AMULET/2 "
# Role, semitones above the root, and an LED color that avoids the colors
# the firmware already uses (tracker orange, glasses violet, alert red,
# broadcast blue).
ROLES = [
    ("root", 0, "00AA80"),
    ("third", 4, "FFD23F"),
    ("fifth", 7, "FF5FA2"),
    ("octave", 12, "7BD389"),
]
NOTE_NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
MIN_FREQUENCY_HZ = 262
MAX_FREQUENCY_HZ = 4186
FIELDS = ["mac", "role", "note", "frequency_hz", "delay_ms", "duration_ms",
          "call_delay_ms", "date"]


def parse_note(name):
    match = re.fullmatch(r"([A-Ga-g])([#b]?)(\d)", name)
    if not match:
        raise ValueError(f"not a note name: {name} (expected e.g. C5 or F#4)")
    letter, accidental, octave = match.groups()
    semitone = NOTE_NAMES[letter.upper()] + {"#": 1, "b": -1, "": 0}[accidental]
    return (int(octave) + 1) * 12 + semitone


def note_label(midi):
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    return f"{names[midi % 12]}{midi // 12 - 1}"


def frequency(midi):
    # Same rounding as the config page's note list.
    return round(440 * 2 ** ((midi - 69) / 12))


def check_timing(name, value, low):
    if not low <= value <= 1000 or value % 10 != 0:
        raise ValueError(f"{name} must be {low}-1000 ms in steps of 10, got {value}")


def load_roles(path):
    if not path.exists():
        return []
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def choose_role(rows, mac):
    for row in rows:
        if row["mac"].lower() == mac.lower():
            return next(role for role in ROLES if role[0] == row["role"]), True
    counts = {role[0]: 0 for role in ROLES}
    for row in rows:
        if row["role"] in counts:
            counts[row["role"]] += 1
    return min(ROLES, key=lambda role: counts[role[0]]), False


def read_line(port, deadline):
    while time.monotonic() < deadline:
        line = port.readline().decode(errors="replace").strip()
        if line:
            return line
    return None


def wait_for(port, expected, seconds):
    deadline = time.monotonic() + seconds
    while (line := read_line(port, deadline)) is not None:
        if line.startswith(PREFIX):
            response = line[len(PREFIX):]
            if any(response.startswith(prefix) for prefix in expected):
                return response
            if response.startswith("ERROR"):
                raise RuntimeError(f"the Amulet reported {response}")
    return None


def set_default(port, verb, payload, saved, config):
    port.reset_input_buffer()
    port.write(f"{PREFIX}{verb} {payload}\n".encode())
    if wait_for(port, [saved], 5) is None:
        raise RuntimeError(f"no reply to {verb}; the firmware may be too old")
    port.reset_input_buffer()
    port.write(f"{PREFIX}{'GET C' if verb == 'DEFAULT C' else 'GET'}\n".encode())
    reply = wait_for(port, [config], 5)
    if reply is None or reply[len(config):].upper() != payload.upper():
        raise RuntimeError(f"read back {reply!r}, expected {config}{payload}")


def provision(port_name, response_payload, call_payload):
    port = serial.Serial()
    port.port = port_name
    port.baudrate = 115200
    port.timeout = 0.2
    # Keep EN and GPIO0 released so opening the port does not reset the board.
    port.dtr = False
    port.rts = False
    port.open()
    try:
        # The board may have printed READY before the port opened; ask again.
        if wait_for(port, ["READY"], 3) is None:
            port.write(f"{PREFIX}PING\n".encode())
            if wait_for(port, ["READY"], 5) is None:
                raise RuntimeError("the Amulet did not answer; is the firmware running?")
        set_default(port, "DEFAULT", response_payload, "OK DEFAULT_SAVED", "CONFIG ")
        if call_payload is not None:
            set_default(port, "DEFAULT C", call_payload, "OK CALL_DEFAULT_SAVED", "CALL_CONFIG ")
    finally:
        port.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port")
    parser.add_argument("--mac")
    parser.add_argument("--root", required=True, help="root note, e.g. C5")
    parser.add_argument("--delay", type=int, default=0, help="delay in ms before answering another board's Call")
    parser.add_argument("--duration", type=int, default=1000, help="note duration in ms")
    parser.add_argument("--call", action="store_true",
                        help="also give the Call the same tone")
    parser.add_argument("--call-delay", type=int, default=0,
                        help="delay in ms before this board's own Call chime "
                             "(with --call); does not affect responses")
    parser.add_argument("--roles-file", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="only validate the root and timings, without a board")
    args = parser.parse_args()
    if not args.check and not (args.port and args.mac and args.roles_file):
        parser.error("--port, --mac and --roles-file are required unless --check is given")

    try:
        root = parse_note(args.root)
        check_timing("--delay", args.delay, 0)
        check_timing("--duration", args.duration, 100)
        check_timing("--call-delay", args.call_delay, 0)
        for _, interval, _ in ROLES:
            hz = frequency(root + interval)
            if not MIN_FREQUENCY_HZ <= hz <= MAX_FREQUENCY_HZ:
                raise ValueError(f"{note_label(root + interval)} is outside C4-C8; pick a lower root")
        if args.check:
            return 0

        rows = load_roles(args.roles_file)
        (role, interval, color), known = choose_role(rows, args.mac)
        hz = frequency(root + interval)
        payload = f"{args.duration}:{args.delay} {hz}:{color}"
        call_payload = f"{args.duration}:{args.call_delay} {hz}:{color}" if args.call else None
        provision(args.port, payload, call_payload)
    except (ValueError, RuntimeError, serial.SerialException) as error:
        where = "Invalid chord-tone options" if args.check else f"Provisioning failed for {args.port}"
        print(f"{where}: {error}", file=sys.stderr)
        return 1

    record = {
        "mac": args.mac.lower(),
        "role": role,
        "note": note_label(root + interval),
        "frequency_hz": hz,
        "delay_ms": args.delay,
        "duration_ms": args.duration,
        "call_delay_ms": args.call_delay if args.call else "",
        "date": datetime.date.today().isoformat(),
    }
    rows = [row for row in rows if row["mac"].lower() != args.mac.lower()] + [record]
    with args.roles_file.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    status = "kept its role" if known else "new board"
    call = f"; Call too, {args.call_delay} ms delay" if args.call else ""
    print(f"{args.mac}: {role} = {note_label(root + interval)} ({hz} Hz), "
          f"{args.duration} ms, {args.delay} ms delay{call} ({status})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
