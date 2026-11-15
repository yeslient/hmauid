#!/system/bin/sh
# Load the module if post-fs-data did not, then start sync-tool: it watches HMA's config,
# evaluates it and pushes the resulting policy over netlink, forever.
# Everything this script and sync-tool create is born private: the log names
# every caller, target and preset, and the token is a key. umask makes the
# default owner-only so a missed chmod cannot widen it; the chmods below are
# the backstop for what already exists. sync-tool inherits this umask, so the
# files it writes (token, mask keys) come out owner-only too.
umask 077
MODDIR=${0%/*}
LOG="$MODDIR/state/sync.log"
mkdir -p "$MODDIR/state"
chmod 700 "$MODDIR/state"

# A previous sync-tool may still be alive: an update that did not reboot, or a
# service.sh that ran twice this boot.
. "$MODDIR/lib.sh"
tosya_kill_orphans
sleep 0.2

# Count what is left with the same exact match the kill used; the pgrep -fc
# rationale lives in lib.sh next to the implementation.
orphans=$(tosya_orphan_pids | grep -c .)

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
# A load that failed leaves nothing to talk to: sync-tool would spin on the
# 10 s retry, growing the log with a full config read every round, looking
# alive while doing nothing. Say so and stop.
if ! tosya_load "$MODDIR" >>"$LOG" 2>&1; then
  echo "[svc] module not loaded; sync-tool not started" >>"$LOG"
  exit 0
fi

[ -x "$MODDIR/sync-tool" ] && "$MODDIR/sync-tool" >>"$LOG" 2>&1 &
