#!/bin/sh
# Send a built firmware to Missingno over Wi-Fi.
#
#   tools/ota_upload.sh path/to/missingno.ino.bin [address]
#
# The address defaults to missingno.local. The password is read from
# missingno/ota_secret.h, which exists only on the machine that made it.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=${1:?usage: tools/ota_upload.sh firmware.bin [address]}
host=${2:-missingno.local}
secret="$root/missingno/ota_secret.h"
[ -f "$secret" ] || { echo "no $secret: this machine holds no key for the device" >&2; exit 1; }
pw=$(sed -n 's|^// password: ||p' "$secret")
[ -n "$pw" ] || { echo "no password line in $secret" >&2; exit 1; }
espota=$( (ls "$HOME"/Library/Arduino15/packages/*/hardware/esp32/*/tools/espota.py \
              "$HOME"/.arduino15/packages/*/hardware/esp32/*/tools/espota.py 2>/dev/null || true) | tail -n 1)
[ -n "$espota" ] || { echo "espota.py not found: install the ESP32 board package" >&2; exit 1; }
# The device fetches the firmware by connecting back to this machine, so a
# firewall must let this Python accept incoming connections. On a Mac the
# system /usr/bin/python3 is usually the one already allowed; set PYTHON to
# use another.
py=${PYTHON:-}
if [ -z "$py" ]; then
  if [ -x /usr/bin/python3 ]; then py=/usr/bin/python3; else py=python3; fi
fi
exec "$py" -I "$espota" -i "$host" -p 3232 -a "$pw" -f "$bin" -r
