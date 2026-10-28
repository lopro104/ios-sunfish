#!/bin/sh
# Keep the phone's default route on Wi-Fi (ew0) while the Wi-Fi link is up, and on the USB network (ue0, 192.168.7.1) otherwise.
# The kernel publishes the lease in `sysctl kern.sweet_wlan_info` ("ip mask router dns ssid"); the link state is ew0's RUNNING flag.
# Run it on the phone (nohup /var/jb/bin/sh sweet-wlan-route.sh &). The USB subnet stays on-link, so SSH keeps working either way.
export PATH=$PATH:/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/bin:/var/jb/sbin
USB_ROUTER=192.168.7.1

# mDNSResponder asks its DNS servers through ue0 (the interface of the only network service sweet-net publishes), so ue0 keeps a scoped
# default route over USB (the PC's NAT) for DNS; the data goes over the default route, which is Wi-Fi while it is up.
route -n add -ifscope ue0 default "$USB_ROUTER" >/dev/null 2>&1

count=0
while :; do
	if ifconfig ew0 2>/dev/null | grep -q RUNNING; then
		set -- $(sysctl -n kern.sweet_wlan_info 2>/dev/null)
		ip=$1
		mask=$2
		router=$3
		if [ -n "$router" ]; then
			# Ensure on-link subnet route exists for ew0
			subnet="${ip%.*}.0"
			route -n add -net "$subnet" -netmask "${mask:-255.255.255.0}" -interface ew0 >/dev/null 2>&1

			if ! route -n get default 2>/dev/null | grep -q "interface: ew0"; then
				ping -c 1 -W 1 "$router" >/dev/null 2>&1
				[ -x /var/jb/usr/bin/sweet-wlan ] && /var/jb/usr/bin/sweet-wlan >/dev/null 2>&1
				route -n delete default >/dev/null 2>&1
				route -n add default "$router" >/dev/null 2>&1
				echo "$(date +%T) default route -> $router via ew0 (sweet-wlan published)"
			fi

			# IPv6: attach it and listen for router advertisements (the kernel only does that after SIOCLL_START + SIOCAUTOCONF_START,
			# which IPConfiguration would do for a configd-managed service) and solicit one with rtsol (the kernel's own solicitation is
			# not answered); then an unscoped default route via the advertising router (the kernel's own one is interface-scoped).
			r6=$(ndp -r 2>/dev/null | sed -n 's/^\(fe80::[^ ]*%ew0\) if=ew0.*/\1/p' | head -1)
			if [ "$v6started" != "$ip" ] || { [ -z "$r6" ] && [ $((count % 10)) -eq 9 ]; }; then
				v6started=$ip
				( sweet-autoconf6 ew0; sleep 2; rtsol ew0 & rp=$!; sleep 8; kill $rp ) >/dev/null 2>&1 &
			fi
			if [ -n "$r6" ] && ! route -n get -inet6 default 2>/dev/null | grep -q "interface: ew0"; then
				route -n delete -inet6 default >/dev/null 2>&1
				route -n add -inet6 default "$r6" >/dev/null 2>&1
				echo "$(date +%T) IPv6 default route -> $r6 via ew0"
			fi

			# Periodic ARP keepalive and health check (every ~15s)
			count=$((count + 1))
			if [ $((count % 5)) -eq 0 ]; then
				if ! ping -c 1 -W 2 "$router" >/dev/null 2>&1; then
					fail_count=$((fail_count + 1))
					if [ "$fail_count" -ge 3 ]; then
						echo "$(date +%T) router ping failed 3 times: auto-reconnecting wlan"
						sysctl -w kern.sweet_wlan_kick=1 >/dev/null 2>&1
						fail_count=0
					fi
				else
					fail_count=0
				fi
			fi
		fi
	else
		count=0
		fail_count=0
		if [ -n "$v6started" ] && route -n get -inet6 default 2>/dev/null | grep -q "interface: ew0"; then
			route -n delete -inet6 default >/dev/null 2>&1
		fi
		v6started=
		if route -n get default 2>/dev/null | grep -q "interface: ew0"; then
			[ -x /var/jb/usr/bin/sweet-wlan ] && /var/jb/usr/bin/sweet-wlan -u >/dev/null 2>&1
			route -n delete default >/dev/null 2>&1
			route -n add default "$USB_ROUTER" >/dev/null 2>&1
			echo "$(date +%T) Wi-Fi down: default route -> $USB_ROUTER via ue0 (sweet-wlan unpublished)"
		fi
	fi
	sleep 3
done
