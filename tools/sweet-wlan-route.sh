#!/bin/sh
# Keep the phone's default route on Wi-Fi (ew0) while the Wi-Fi link is up, and on the USB network (ue0, 192.168.7.1) otherwise.
# The kernel publishes the lease in `sysctl kern.sweet_wlan_info` ("ip mask router dns ssid"); the link state is ew0's RUNNING flag.
# Run it on the phone (nohup /var/jb/bin/sh sweet-wlan-route.sh &). The USB subnet stays on-link, so SSH keeps working either way.
export PATH=$PATH:/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/bin:/var/jb/sbin
USB_ROUTER=192.168.7.1

# mDNSResponder asks its DNS servers through ue0 (the interface of the only network service sweet-net publishes), so ue0 keeps a scoped
# default route over USB (the PC's NAT) for DNS; the data goes over the default route, which is Wi-Fi while it is up.
route -n add -ifscope ue0 default "$USB_ROUTER" >/dev/null 2>&1

while :; do
	if ifconfig ew0 2>/dev/null | grep -q RUNNING; then
		set -- $(sysctl -n kern.sweet_wlan_info 2>/dev/null)
		router=$3
		if [ -n "$router" ] && ! route -n get default 2>/dev/null | grep -q "interface: ew0"; then
			ping -b ew0 -c 1 -W 1 "$router" >/dev/null 2>&1
			[ -x /var/jb/usr/bin/sweet-wlan ] && /var/jb/usr/bin/sweet-wlan >/dev/null 2>&1
			route -n delete default >/dev/null 2>&1
			route -n add default "$router" >/dev/null 2>&1
			echo "$(date +%T) default route -> $router via ew0 (sweet-wlan published)"
		fi
	else
		if route -n get default 2>/dev/null | grep -q "interface: ew0"; then
			[ -x /var/jb/usr/bin/sweet-wlan ] && /var/jb/usr/bin/sweet-wlan -u >/dev/null 2>&1
			route -n delete default >/dev/null 2>&1
			route -n add default "$USB_ROUTER" >/dev/null 2>&1
			echo "$(date +%T) Wi-Fi down: default route -> $USB_ROUTER via ue0 (sweet-wlan unpublished)"
		fi
	fi
	sleep 3
done
