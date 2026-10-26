#!/bin/sh
# Host side of the phone's USB Ethernet (kernel sweetusb=2, patches 0075-0077).
#   tools/sweet-usbnet.sh [LOGFILE]
# Waits for the phone's CDC-ECM interface (host MAC 02:00:5e:77:ee:01),
# gives it 192.168.7.1/24 (the phone is 192.168.7.2), then prints the live
# kernel console the phone streams to UDP port 5140 (and appends it to
# LOGFILE if given). Needs sudo for the address.
set -eu

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	echo "Usage: $(basename "$0") [LOGFILE]"
	echo "Wait for the phone's USB Ethernet interface and stream the live kernel UDP console."
	exit 0
fi

mac=02:00:5e:77:ee:01
echo "waiting for the phone's USB Ethernet ($mac)..." >&2
until ip -o link | grep -qi "$mac"; do sleep 1; done
ifname=$(ip -o link | grep -i "$mac" | awk -F': ' '{print $2}' | cut -d@ -f1)
nmcli dev set "$ifname" managed no 2>/dev/null || true
sudo ip addr replace 192.168.7.1/24 dev "$ifname"
sudo ip link set "$ifname" up
echo "$ifname up: 192.168.7.1 <-> phone 192.168.7.2; live log on udp/5140" >&2

# Internet for the phone (its sweet-net job publishes 192.168.7.1 as its router, DNS 1.1.1.1):
# forwarding and NAT out of the PC's default route. Runtime only; SWEET_NO_NAT=1 skips it.
if [ -z "${SWEET_NO_NAT:-}" ]; then
	uplink=$(ip route show default | awk '{print $5; exit}')
	sudo sysctl -qw net.ipv4.ip_forward=1
	if command -v firewall-cmd >/dev/null 2>&1 && sudo firewall-cmd -q --state 2>/dev/null; then
		sudo firewall-cmd -q --zone=trusted --change-interface="$ifname"
		zone=$(sudo firewall-cmd --get-zone-of-interface="$uplink" 2>/dev/null || sudo firewall-cmd --get-default-zone)
		sudo firewall-cmd -q --zone="$zone" --add-masquerade
	else
		sudo iptables -t nat -C POSTROUTING -s 192.168.7.0/24 -o "$uplink" -j MASQUERADE 2>/dev/null ||
		    sudo iptables -t nat -A POSTROUTING -s 192.168.7.0/24 -o "$uplink" -j MASQUERADE
	fi
	# Docker's FORWARD policy is DROP: let the phone's traffic through it.
	sudo iptables -C FORWARD -i "$ifname" -j ACCEPT 2>/dev/null || sudo iptables -I FORWARD -i "$ifname" -j ACCEPT
	sudo iptables -C FORWARD -o "$ifname" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null ||
	    sudo iptables -I FORWARD -o "$ifname" -m state --state RELATED,ESTABLISHED -j ACCEPT
	echo "NAT: 192.168.7.0/24 out through $uplink" >&2
fi

listen_udp() {
	port=5140
	if command -v python3 >/dev/null 2>&1; then
		# any sender, any netcat flavour (nmap's ncat refuses -k with UDP)
		python3 -u -c 'import socket,sys
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
s.bind(("",int(sys.argv[1])))
while True:
    sys.stdout.buffer.write(s.recv(65536)); sys.stdout.flush()' "$port"
	elif command -v socat >/dev/null 2>&1; then
		socat -u "udp-recv:$port,reuseaddr" -
	elif command -v ncat >/dev/null 2>&1; then
		ncat -l -u -k "$port"
	elif nc -h 2>&1 | grep -q -- '-k'; then
		nc -ukl "$port"
	else
		while true; do
			nc -u -l -p "$port" 2>/dev/null || nc -u -l "$port" 2>/dev/null || break
		done
	fi
}

if [ $# -ge 1 ]; then
	listen_udp | tee -a "$1"
else
	listen_udp
fi
