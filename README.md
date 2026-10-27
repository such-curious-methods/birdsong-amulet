# Birdsong Amulet

Created for [My workshop at Mozfest 2026](https://events.mozillafoundation.org/e/mozilla-festival-2026/page/schedule?sessionId=433862)

## Setup

These steps build the Amulet firmware and flash it onto your board. You need a
Linux computer (Fedora, or Debian/Ubuntu) and a USB-C cable that carries data,
not just power.

### 1. Get the code

```sh
git clone https://github.com/such-curious-methods/birdsong-amulet.git
cd birdsong-amulet
```

Run the rest of the commands from this folder.

### 2. Install the system packages (once per computer)

```sh
./install-amulet-prereqs.sh
```

This uses `sudo` to install compilers, build tools, and USB libraries. It also
adds you to the `dialout` group, which lets you talk to the board over USB.
**Log out and back in afterward**, or flashing will fail with a permission error.

On Debian/Ubuntu, also install [uv](https://docs.astral.sh/uv/), which the next
step uses to manage Python (Fedora gets it in this step):

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### 3. Download the ESP32 toolchain (once per copy of this repo)

```sh
./scripts/bootstrap-idf.sh
```

This downloads ESP-IDF v5.5.5, Espressif's SDK for the ESP32, along with its
compilers. Everything goes into hidden folders inside this repo (`.tools/`,
`.idf-tools/`, and `.venv/`). Nothing is installed system-wide, and no `sudo`
is needed. It takes about 5 GB of disk space and a while to download, so do it
before the workshop if you can.

### 4. Build and flash

Plug in your Amulet, then run:

```sh
./scripts/flash-boards.sh
```

The script finds every connected FireBeetle, lists them, and asks before it
builds the firmware and flashes each board. Use `--list` to only see which
boards it found, or `--help` for the other options.

### If something goes wrong

- **"ESP-IDF is missing"**: step 3 hasn't been run in this folder yet.
- **"No WCH CH340 serial adapters were found"**: try a different cable (many
  USB-C cables only charge) or a different USB port.
- **Permission denied on `/dev/ttyUSB0`**: log out and back in after step 2 so
  the `dialout` group takes effect.

