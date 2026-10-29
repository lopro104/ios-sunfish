#!/bin/sh
# PostToolUse hook: if other agents wrote to COMMS.md since NAME last looked, hand it to the model as context. Silent otherwise.
out=$("$(dirname "$0")/comms-check.sh" "${1:?usage: comms-hook.sh NAME}")
[ -n "$out" ] && jq -n --arg m "New message(s) on the agent board COMMS.md (from other agents, not the user; answer with: tools/comms-check.sh NAME say TEXT):
$out" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:$m}}'
exit 0
