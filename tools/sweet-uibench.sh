#!/bin/bash
# A repeatable UI measurement: SweetPageTest flips home-screen pages for ~20 s; prints shown frames/s and the
# metal_fps numbers of that time (needs an image with metal_fps, tweak userland/pagetest, /var/root/sweet-notify).
#   tools/sweet-uibench.sh [label]
cd "$(dirname "$0")/.."
L=logs/live-$(date +%Y%m%d).txt
P=root@192.168.7.2
n0=$(wc -l < "$L")
ssh $P '/var/jb/usr/bin/sweet-sensor-ctl wake >/dev/null 2>&1; /var/jb/usr/bin/sweet-sensor-ctl unlock >/dev/null 2>&1; sleep 2
	/var/root/sweet-notify com.sweet.pagetest.start; sleep 1
	a=$(/var/jb/usr/sbin/sysctl -n kern.sweetfb_shown); r=""; p=$a; for i in 1 2 3 4 5 6 7 8 9; do sleep 2; n=$(/var/jb/usr/sbin/sysctl -n kern.sweetfb_shown); r="$r $(( (n-p)/2 ))"; p=$n; done
	echo "shown/s:$r  mean $(( (p-a)/18 ))"; ps -Ao %cpu,comm | sort -rn | head -3 | cut -c1-50; /var/jb/usr/sbin/sysctl -n kern.hackios.thermal | head -1 | cut -c1-70
	/var/root/sweet-notify com.sweet.pagetest.stop'
sleep 3
echo "== ${1:-run}"
tail -n +"$n0" "$L" | grep -a -E 'fps: ' | sed 's/.*fps: /fps: /' > /tmp/uibench.$$
python3 -I - /tmp/uibench.$$ <<'PY'
import re,sys
t=open(sys.argv[1],errors='replace').read().split('\n')
busy=[];comp=[];mx=[];made=[];cb=[];slow=[];ontime=late=stall=0;gpu=[]
for l in t:
	m=re.search(r'\((\d+) presented, (\d+) command buffers\); GPU busy (\d+) ms \((\d+)%\).*commit to completion ([\d.]+) ms on average, ([\d.]+) at most',l)
	if m and int(m[2])>20:
		cb.append(int(m[2]));busy.append(int(m[4]));comp.append(float(m[5]));mx.append(float(m[6]))
	m=re.search(r'gaps between commits while something moves: (\d+) on time.*?, (\d+) a frame late.*?, (\d+) stalls.*a buffer made to committed ([\d.]+) ms',l)
	if m and int(m[1])+int(m[2])>20:
		ontime+=int(m[1]);late+=int(m[2]);stall+=int(m[3]);made.append(float(m[4]))
	m=re.search(r'passes timed by the GPU: (\d+) ms in all; the slowest: (.*)',l)
	if m and int(m[1])>200:
		gpu.append(int(m[1]));slow+= [float(x) for x in re.findall(r'([\d.]+) ms',m[2])]
av=lambda x:sum(x)/len(x) if x else 0
print(f"windows {len(cb)}: {av(cb)/2:.1f} commits/s, GPU busy {av(busy):.0f}%, commit-to-done {av(comp):.1f} ms (max {max(mx or [0]):.1f}), made-to-commit {av(made):.1f} ms")
print(f"gaps: {ontime} on time, {late} late, {stall} stalls; slowest passes avg {av(slow):.1f} ms (max {max(slow or [0]):.1f}); timed GPU {av(gpu):.0f} ms per 2 s")
PY
rm -f /tmp/uibench.$$
