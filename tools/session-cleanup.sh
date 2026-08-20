#!/bin/sh
# Stop the things a working session leaves running: emulators and the
# screenshot gallery server.
#
# Run automatically by the SessionEnd hook in .claude/settings.json, and safe
# to run by hand at any time.
#
# Every `pebble install --emulator` starts an emulator that nothing ever reaps.
# The processes reparent to init, so they outlive the session that launched
# them, and an idle qemu-pebble still burns ~8% of a core because it does not
# idle-halt. Thirteen had accumulated by 2026-08-20, ~200% CPU between them.
#
# This deliberately does not call the `pebble` CLI. `pebble kill` stops only
# the pair named in /tmp/pb-emulator.json and is blind to every earlier one,
# and a wedged SDK can hang it for minutes -- which would stall the very exit
# this script runs on.

# The hook feeds a JSON payload on stdin. Skip /clear: clearing context
# mid-session should not kill an emulator or gallery still being used.
if [ ! -t 0 ]; then
    if cat | grep -q '"reason"[[:space:]]*:[[:space:]]*"clear"'; then
        exit 0
    fi
fi

# -x matches the process name, so it can never match the shell running this.
pkill -TERM -x qemu-pebble

# pypkjs is matched on cmdline, which is dangerous on its own: `pkill -f`
# hits any shell whose command line merely mentions the name, including the
# one running this. Requiring the process name to be python confines it to a
# real interpreter -- a shell is comm=bash and can never match.
ps -eo pid=,comm=,args= | awk '$2 ~ /^python/ && /pypkjs/ {print $1}' |
    while read -r pid; do
        [ "$pid" = "$$" ] || kill -TERM "$pid" 2>/dev/null
    done

# SIGTERM only, never SIGKILL: it corrupts the emulator's flash image.
rm -f /tmp/pb-emulator.json

# The gallery server, addressed by listening port rather than by pattern.
pid=$(ss -ltnpH 'sport = :8477' 2>/dev/null | grep -o 'pid=[0-9]*' | cut -d= -f2 | head -1)
[ -n "$pid" ] && kill -TERM "$pid"

# Never let a cleanup failure surface as a hook error.
exit 0
