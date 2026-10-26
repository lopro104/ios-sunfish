#!/bin/bash
# Keeps the phone's USB network up on the PC side and records the live log, until killed:
#   tools/sweet-live.sh            (logs/live-YYYYMMDD.txt, appended)
# Every 3 s: when the phone's interface (MAC 02:00:5e:77:ee:01) is there without 192.168.7.1, gives
# it the address and NAT again (NetworkManager drops it; the phone re-enumerates on each boot).
HERE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$HERE/logs"
python3 -u -c 'import socket,sys,time,os
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("",5140))
while True:
    d=s.recv(65536)
    with open(os.path.join(sys.argv[1],"live-"+time.strftime("%Y%m%d")+".txt"),"ab") as f: f.write(d)' "$HERE/logs" &
LISTENER=$!
trap 'kill $LISTENER 2>/dev/null' EXIT
while kill -0 $LISTENER 2>/dev/null; do
	IF=$(ip -o link | grep -i '02:00:5e:77:ee:01' | awk -F': ' '{print $2}' | cut -d@ -f1)
	if [ -n "$IF" ] && ! ip -o addr show dev "$IF" | grep -q 192.168.7.1/24; then
		nmcli dev set "$IF" managed no 2>/dev/null
		sudo ip addr replace 192.168.7.1/24 dev "$IF" && sudo ip link set "$IF" up
		sudo firewall-cmd -q --zone=trusted --change-interface="$IF" 2>/dev/null
		sudo iptables -C FORWARD -i "$IF" -j ACCEPT 2>/dev/null || sudo iptables -I FORWARD -i "$IF" -j ACCEPT
		sudo iptables -C FORWARD -o "$IF" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null ||
		    sudo iptables -I FORWARD -o "$IF" -m state --state RELATED,ESTABLISHED -j ACCEPT
		echo "$(date +%H:%M:%S) $IF: 192.168.7.1 + NAT"
	fi
	sleep 3
done
