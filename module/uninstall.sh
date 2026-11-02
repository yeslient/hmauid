#!/system/bin/sh
# Runs when the module manager removes the module, with this directory still in
# place. The kernel side needs no line here: the module takes itself off the
# kernel's module list (src/main.c), so there is nothing rmmod can name and the
# ko disappears with the reboot a removal asks for. What does outlive the
# directory is the sync-tool service.sh started with &, so that is what this
# script is for.
MODDIR=${0%/*}

# Exact cmdline match, not pkill -f: -f matches a substring ERE over the whole
# command line, so anything that merely mentions this path would be hit too.
# The pid needs two expansions: ${p#/proc/} leaves "<pid>/cmdline" and the
# suffix strip turns that into the pid.
killed=0
for p in /proc/[0-9]*/cmdline; do
  pid=${p#/proc/}
  pid=${pid%/cmdline}
  if [ "$(tr '\0' ' ' < "$p" 2>/dev/null)" = "$MODDIR/sync-tool " ]; then
    kill "$pid" 2>/dev/null && killed=$((killed + 1))
  fi
done
sleep 0.2
orphans=$(pgrep -fc "$MODDIR/sync-tool" 2>/dev/null)
echo "[uninst] killed $killed, orphans left: ${orphans:-0}"

# The boot-scoped marker says "this boot loaded once"; leaving it behind would
# let a later same-boot load.sh read a stale answer.
rm -f "$MODDIR/state/.loaded" "$MODDIR/state/token"
# state/sync.log stays: once the module is gone it is the only record of what
# it did. Removing the whole directory is the module manager's job, not ours.
