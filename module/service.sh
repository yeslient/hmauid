#!/system/bin/sh
# Load the module if post-fs-data did not, then start sync-tool: it watches HMA's config,
# evaluates it and pushes the resulting policy over netlink, forever.
MODDIR=${0%/*}
LOG="$MODDIR/state/sync.log"
mkdir -p "$MODDIR/state"
# The log names every caller, target and preset; keep both root-only instead of
# whatever the filesystem hands a fresh directory.
chmod 700 "$MODDIR/state"

# A previous sync-tool may still be alive: an update that did not reboot, or a
# service.sh that ran twice this boot. Exact cmdline match, not pkill -f: -f
# matches a substring ERE over the whole command line, so anything that merely
# mentions this path would be hit too. The pid needs two expansions:
# ${p#/proc/} leaves "<pid>/cmdline" and the suffix strip turns that into the
# pid.
for p in /proc/[0-9]*/cmdline; do
  pid=${p#/proc/}
  pid=${pid%/cmdline}
  [ "$(tr '\0' ' ' < "$p" 2>/dev/null)" = "$MODDIR/sync-tool " ] &&
    kill "$pid" 2>/dev/null
done
sleep 0.2

# Count what is left with the same exact match the kill used, and not with
# `pgrep -fc`: that one matches the path as a substring of any command line,
# so the very shell running this script (or an uninstaller mentioning the
# path) would be counted as an orphan of its own crime.
orphans=0
for p in /proc/[0-9]*/cmdline; do
  [ "$(tr '\0' ' ' < "$p" 2>/dev/null)" = "$MODDIR/sync-tool " ] &&
    orphans=$((orphans + 1))
done

# Rotate here and only here, between the kill and the start. The running
# sync-tool holds the log fd, so an mv while it lives leaves it writing into
# the renamed inode: the fresh sync.log would stay empty until reboot, and the
# rotated copy would keep growing forever. Measured idle growth is ~120 KiB/h,
# so 4 MiB is about eight days of uptime -- it never triggers within one boot,
# which is why the check lives at boot only.
if [ "$(stat -c %s "$LOG" 2>/dev/null || echo 0)" -gt 4194304 ]; then
  mv -f "$LOG" "$LOG.1"
fi
touch "$LOG" && chmod 600 "$LOG"
echo "[svc] orphans left: $orphans" >>"$LOG"

. "$MODDIR/load.sh"
tosya_load "$MODDIR" >>"$LOG" 2>&1

[ -x "$MODDIR/sync-tool" ] && "$MODDIR/sync-tool" >>"$LOG" 2>&1 &
