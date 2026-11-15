#!/system/bin/sh
# Runs when the module manager removes the module, with this directory still in
# place. The kernel side needs no line here: the module takes itself off the
# kernel's module list (src/main.c), so there is nothing rmmod can name and the
# ko disappears with the reboot a removal asks for. What does outlive the
# directory is the sync-tool service.sh started with &, so that is what this
# script is for.
MODDIR=${0%/*}
. "$MODDIR/lib.sh"

killed=0
for pid in $(tosya_orphan_pids); do
  kill "$pid" 2>/dev/null && killed=$((killed + 1))
done
sleep 0.2
orphans=$(tosya_orphan_pids | grep -c .)
echo "[uninst] killed $killed, orphans left: $orphans"

# The boot-scoped marker says "this boot loaded once"; leaving it behind would
# let a later same-boot load.sh read a stale answer.
rm -f "$MODDIR/state/.loaded" "$MODDIR/state/token"
# state/sync.log stays: once the module is gone it is the only record of what
# it did. Removing the whole directory is the module manager's job, not ours.
