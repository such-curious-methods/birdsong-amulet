# Print the Bluetooth Low Energy advertisements an ESP32 hears, to help
# research new Amulet signatures. Runs on MicroPython (for example from
# Thonny); installing MicroPython replaces the Amulet firmware.
#
# Like the Amulet, it scans passively and never shows Bluetooth addresses or
# device names by default. Each device gets a short label that is random per
# run, so packets from one device can be grouped without identifying it.
#
# Settings below; stop with Ctrl-C.

import hashlib
import os
import time

try:
    from micropython import const
except ImportError:  # CPython, for testing the parser
    def const(value):
        return value

# Seconds to scan; 0 scans until stopped.
DURATION_S = 60
# Ignore anything weaker than this. Raise it (for example to -50) and hold
# your own device next to the board to capture only that device.
MIN_RSSI = -100
# Print only advertisements with this manufacturer company ID, or None.
ONLY_COMPANY_ID = None
# Print an identical advertisement from the same device again only after this
# many seconds.
REPEAT_S = 10
# Show local names such as "Chris's AirPods". Off by default: names are often
# personal. Turn on only when capturing your own devices.
SHOW_NAMES = False

_IRQ_SCAN_RESULT = const(5)
_IRQ_SCAN_DONE = const(6)
# Listen all the time (30 ms window every 30 ms) rather than the default
# 11.25 ms per 1.28 s, which misses most advertisements.
_SCAN_INTERVAL_US = const(30000)
_SCAN_WINDOW_US = const(30000)
_QUEUE_LIMIT = const(64)

ADV_TYPES = {
    0: "connectable",
    1: "directed",
    2: "scannable",
    3: "non-connectable",
    4: "scan response",
}

# A few common Bluetooth SIG company IDs; the rest print as numbers.
COMPANIES = {
    0x0006: "Microsoft",
    0x004C: "Apple",
    0x0075: "Samsung",
    0x00E0: "Google",
    0x01AB: "Meta",
    0x058E: "Meta Technologies",
    0x0D53: "Luxottica",
    0x03C2: "Snap",
    0xFFFF: "test / unassigned",
}

# A few common 16-bit service UUIDs.
SERVICES = {
    0xFD5A: "Samsung SmartTag",
    0xFEAA: "Google (Eddystone / Find Hub)",
    0xFCB2: "DULT",
    0xFEED: "Tile",
    0xFFFA: "ASTM Remote ID (drones)",
    0xFE2C: "Google Fast Pair",
}


def hex_bytes(data):
    return " ".join("%02x" % byte for byte in data)


def read_le16(data):
    return data[0] | (data[1] << 8)


def parse_fields(adv_data):
    """Splits advertisement data into (type, value) pairs; None if malformed."""
    fields = []
    offset = 0
    while offset < len(adv_data):
        length = adv_data[offset]
        if length == 0 or offset + 1 + length > len(adv_data):
            return None
        fields.append((adv_data[offset + 1], bytes(adv_data[offset + 2:offset + 1 + length])))
        offset += 1 + length
    return fields


def company_id(fields):
    for kind, value in fields:
        if kind == 0xFF and len(value) >= 2:
            return read_le16(value)
    return None


def describe_field(kind, value, show_names=False):
    if kind == 0x01 and value:
        return "flags 0x%02x" % value[0]
    if kind in (0x02, 0x03) and len(value) % 2 == 0:
        uuids = []
        for index in range(0, len(value), 2):
            uuid = read_le16(value[index:index + 2])
            label = SERVICES.get(uuid)
            uuids.append("0x%04X" % uuid + (" (%s)" % label if label else ""))
        return "16-bit services " + ", ".join(uuids)
    if kind in (0x06, 0x07):
        return "128-bit services " + hex_bytes(value)
    if kind in (0x08, 0x09):
        if show_names:
            try:
                return "name %r" % value.decode("utf-8")
            except UnicodeError:
                return "name (not text): " + hex_bytes(value)
        return "name (hidden, %d bytes)" % len(value)
    if kind == 0x0A and value:
        power = value[0] - 256 if value[0] > 127 else value[0]
        return "tx power %d dBm" % power
    if kind == 0x16 and len(value) >= 2:
        uuid = read_le16(value)
        label = SERVICES.get(uuid)
        return "service data 0x%04X%s: %s" % (
            uuid, " (%s)" % label if label else "", hex_bytes(value[2:]))
    if kind == 0xFF and len(value) >= 2:
        company = read_le16(value)
        label = COMPANIES.get(company)
        return "manufacturer 0x%04X%s: %s" % (
            company, " (%s)" % label if label else "", hex_bytes(value[2:]))
    return "type 0x%02X: %s" % (kind, hex_bytes(value))


def device_label(salt, addr):
    """A short label that groups one device's packets for this run only."""
    return "dev-" + hex_bytes(hashlib.sha256(salt + bytes(addr)).digest()[:3]).replace(" ", "")


def raw_hex(adv_data, show_names=False):
    """Raw bytes, with local-name bytes masked as "xx" unless names are shown.

    A malformed advertisement cannot be split into fields, so with names
    hidden its bytes are withheld entirely rather than risk showing a name.
    """
    if show_names:
        return hex_bytes(adv_data)
    if parse_fields(adv_data) is None:
        return "(%d bytes withheld; set SHOW_NAMES = True to see them)" % len(adv_data)
    parts = []
    offset = 0
    while offset < len(adv_data):
        length = adv_data[offset]
        is_name = adv_data[offset + 1] in (0x08, 0x09)
        parts.append("%02x %02x" % (length, adv_data[offset + 1]))
        value = adv_data[offset + 2:offset + 1 + length]
        if value:
            parts.append(" ".join("xx" for _ in value) if is_name else hex_bytes(value))
        offset += 1 + length
    return " ".join(parts)


def format_report(label, addr_type, adv_type, rssi, adv_data, show_names=False):
    lines = ["%s  %d dBm  %s address  %s" % (
        label, rssi, "public" if addr_type == 0 else "random",
        ADV_TYPES.get(adv_type, "type %d" % adv_type))]
    lines.append("  raw  " + raw_hex(adv_data, show_names))
    fields = parse_fields(adv_data)
    if fields is None:
        lines.append("  (malformed advertisement)")
    else:
        for kind, value in fields:
            lines.append("  " + describe_field(kind, value, show_names))
    return "\n".join(lines)


def main():
    import bluetooth

    salt = os.urandom(8)
    queue = []
    last_printed = {}
    state = {"done": False}

    def irq(event, data):
        if event == _IRQ_SCAN_RESULT:
            addr_type, addr, adv_type, rssi, adv_data = data
            # The buffers are only valid during this call, so copy them.
            if rssi >= MIN_RSSI and len(queue) < _QUEUE_LIMIT:
                queue.append((addr_type, bytes(addr), adv_type, rssi, bytes(adv_data)))
        elif event == _IRQ_SCAN_DONE:
            state["done"] = True

    ble = bluetooth.BLE()
    ble.active(True)
    ble.irq(irq)
    ble.gap_scan(DURATION_S * 1000, _SCAN_INTERVAL_US, _SCAN_WINDOW_US, False)
    print("Scanning%s; press Ctrl-C to stop." % (
        " for %d s" % DURATION_S if DURATION_S else ""))

    try:
        while not state["done"] or queue:
            if not queue:
                time.sleep_ms(50)
                continue
            addr_type, addr, adv_type, rssi, adv_data = queue.pop(0)
            fields = parse_fields(adv_data)
            if ONLY_COMPANY_ID is not None and (
                    fields is None or company_id(fields) != ONLY_COMPANY_ID):
                continue
            label = device_label(salt, addr)
            key = (label, adv_data)
            now = time.ticks_ms()
            seen = last_printed.get(key)
            if seen is not None and time.ticks_diff(now, seen) < REPEAT_S * 1000:
                continue
            last_printed[key] = now
            if len(last_printed) > 200:
                last_printed.clear()
            print(format_report(label, addr_type, adv_type, rssi, adv_data, SHOW_NAMES))
    except KeyboardInterrupt:
        pass
    finally:
        ble.gap_scan(None)
        ble.active(False)
        print("Scan stopped.")


if __name__ == "__main__":
    main()
