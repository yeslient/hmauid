#!/system/bin/sh
# One implementation of the sync-tool orphan scan, sourced by service.sh
# (kill before a fresh start) and uninstall.sh (the final sweep). $MODDIR
# must be set by the sourcing script.
#
# Exact cmdline match, not pkill -f: -f matches a substring ERE over the
# whole command line, so anything that merely mentions the path would be
# hit too -- including the very script doing the sweeping, or an uninstaller
# that names it. The pid needs two expansions: ${p#/proc/} leaves
# "<pid>/cmdline" and the suffix strip turns that into the pid.

# Print the pid of every live sync-tool, one per line.
tosya_orphan_pids() {
  for p in /proc/[0-9]*/cmdline; do
    [ "$(tr '\0' ' ' < "$p" 2>/dev/null)" = "$MODDIR/sync-tool " ] || continue
    p=${p#/proc/}
    echo "${p%/cmdline}"
  done
}

# Kill every live sync-tool. Best effort by design: a pid can vanish between
# the scan and the kill, and the count afterwards is what reports the truth.
tosya_kill_orphans() {
  for pid in $(tosya_orphan_pids); do
    kill "$pid" 2>/dev/null
  done
}
