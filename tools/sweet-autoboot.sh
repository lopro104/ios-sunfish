#!/bin/bash
# Re-boot the phone into an image whenever it shows up in fastboot (e.g. after a kernel panic reset while nobody is home).
# usage: tools/sweet-autoboot.sh shim/out/shim-local-XXXX.img [serial]
# Only temporary `fastboot boot`, never a flash; `set_active b` first. Stops with Ctrl-C.
img=${1:?usage: sweet-autoboot.sh IMAGE [SERIAL]}
serial=${2:-$SWEET_SERIAL}
[ -f "$img" ] || { echo "no such image: $img" >&2; exit 1; }
last=0
while :; do
	if fastboot devices 2>/dev/null | grep -q "^$serial[[:space:]]"; then
		now=$(date +%s)
		if [ $((now - last)) -ge 60 ]; then
			echo "$(date +%T) $serial in fastboot: booting $img"
			fastboot -s "$serial" set_active b && fastboot -s "$serial" boot "$img"
			last=$now
		fi
	fi
	sleep 3
done
