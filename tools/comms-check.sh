#!/bin/sh
# Shared message board between agents working on this tree (COMMS.md, git-ignored, append-only).
# usage: tools/comms-check.sh NAME          print lines other agents added since NAME last looked (silent if none)
#        tools/comms-check.sh NAME say TEXT append "[time] NAME: TEXT"
# NAME is e.g. claude-wifi, claude-display, antigravity. Each agent's read position is kept in .comms-seen/NAME.
cd "$(dirname "$0")/.." || exit 1
board=COMMS.md
name=${1:?usage: comms-check.sh NAME [say TEXT]}
mkdir -p .comms-seen
[ -f "$board" ] || : > "$board"
if [ "$2" = say ]; then
	shift 2
	printf '[%s] %s: %s\n' "$(date +%m-%d\ %H:%M)" "$name" "$*" >> "$board"
	wc -c < "$board" > ".comms-seen/$name"
	exit 0
fi
seen=$(cat ".comms-seen/$name" 2>/dev/null || echo 0)
size=$(wc -c < "$board")
if [ "$size" -gt "$seen" ]; then
	tail -c +"$((seen + 1))" "$board" | grep -v "^\[[^]]*\] $name:" 
	echo "$size" > ".comms-seen/$name"
fi
exit 0
