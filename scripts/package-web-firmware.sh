#!/usr/bin/env bash
# Build the FireBeetle firmware and package it for the browser flasher in
# web/flash.html.
#
# Usage: ./scripts/package-web-firmware.sh
#
# Writes into web/firmware/:
#   bootloader-VERSION.bin, partition-table-VERSION.bin, amulet-VERSION.bin
#       written separately by "Update", which leaves the NVS partition (and
#       the attendee's saved melodies) untouched;
#   amulet-merged-VERSION.bin
#       one image at 0x0 for "Fresh install", which erases everything;
#   manifest-update.json, manifest-fresh.json
#       ESP Web Tools manifests pointing at those files.
# Version-stamped filenames keep browsers and CDNs from serving stale images.
#
# web/firmware/ is excluded from this repository through .git/info/exclude
# (not .gitignore, which publish-code.sh copies to the public repository).
# publish-code.sh mirrors it into the public repository, where it is committed
# for GitHub Pages. Re-run this script before publishing.
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$project_root/build/firebeetle"
out_dir="$project_root/web/firmware"
cd -- "$project_root"

"$project_root/scripts/idf.sh" \
    -B build/firebeetle \
    -D IDF_TARGET=esp32 \
    -D SDKCONFIG="$build_dir/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="$project_root/sdkconfig.firebeetle.defaults" \
    build

version="$(date -u +%Y.%m.%d)-$(git rev-parse --short HEAD)"
if [[ -n "$(git status --porcelain -- main sdkconfig.defaults sdkconfig.firebeetle.defaults CMakeLists.txt)" ]]; then
    version="$version-dirty"
fi

mkdir -p -- "$out_dir"
rm -f -- "$out_dir"/*.bin
cp -- "$build_dir/bootloader/bootloader.bin" "$out_dir/bootloader-$version.bin"
cp -- "$build_dir/partition_table/partition-table.bin" "$out_dir/partition-table-$version.bin"
cp -- "$build_dir/amulet.bin" "$out_dir/amulet-$version.bin"

# flash_args carries the flash mode, frequency, size and the three offsets
# (bootloader 0x1000, partition table 0x8000, app 0x10000).
(cd -- "$build_dir" && "$project_root/.venv/bin/python" -m esptool --chip esp32 \
    merge_bin -o "$out_dir/amulet-merged-$version.bin" @flash_args)

# Update: separate parts, and an unchecked erase box instead of ESP Web
# Tools' default full-chip erase for boards without Improv.
cat > "$out_dir/manifest-update.json" <<EOF
{
  "name": "Birdsong Amulet",
  "version": "$version",
  "new_install_prompt_erase": true,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "bootloader-$version.bin", "offset": 4096 },
        { "path": "partition-table-$version.bin", "offset": 32768 },
        { "path": "amulet-$version.bin", "offset": 65536 }
      ]
    }
  ]
}
EOF

# Fresh install: no erase prompt, so ESP Web Tools erases the whole chip.
cat > "$out_dir/manifest-fresh.json" <<EOF
{
  "name": "Birdsong Amulet",
  "version": "$version",
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "amulet-merged-$version.bin", "offset": 0 }
      ]
    }
  ]
}
EOF

printf 'Packaged firmware %s into %s\n' "$version" "$out_dir"
