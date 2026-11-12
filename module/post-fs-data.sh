#!/system/bin/sh
# Install hooks before the framework starts. Policies and APK paths are sent by
# Owner's sync-tool from service.sh once their source files are available.
# Same rule as service.sh: everything here is born private, the chmods are the
# backstop for what already exists.
umask 077
MODDIR=${0%/*}
LOG="$MODDIR/state/sync.log"
mkdir -p "$MODDIR/state"
chmod 700 "$MODDIR/state"
touch "$LOG" && chmod 600 "$LOG"
# The boot-scoped loaded marker (see load.sh): this stage owns clearing it.
rm -f "$MODDIR/state/.loaded"
. "$MODDIR/load.sh"
tosya_load "$MODDIR" >>"$LOG" 2>&1
