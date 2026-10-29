#!/system/bin/sh
# Shared by the two boot stages. An inline module stays loaded until reboot;
# never unload or stack a replacement over an existing instance.
#
# The loaded check cannot be lsmod: the module takes its own name off the
# kernel's module list (src/main.c), so /proc/modules never shows it and an
# lsmod guard would read as "not loaded" every single boot -- post-fs-data
# would load, service.sh would insmod a second copy on top. A boot-scoped
# marker does instead: post-fs-data clears it before the first load attempt,
# and finding it here means this boot has already loaded once.
tosya_load() {
  local moddir="$1"
  local ko="$moddir/ko/tosya.ko"
  local marker="$moddir/state/.loaded"
  local loader rc

  [ -f "$ko" ] || { echo "[load] no selected kernel module"; return 1; }
  if [ -f "$marker" ]; then
    echo "[load] already loaded this boot; replacing the module requires a reboot"
    return 0
  fi

  # Use the installed KernelSU loader when available. The bundled loader keeps
  # Owner's Magisk/non-KernelSU path; a failed ksud load is not retried through
  # another loader. No shell-evaluated parameter file is involved.
  if [ -x /data/adb/ksud ]; then
    loader=ksud
    /data/adb/ksud insmod "$ko"
    rc=$?
  elif [ -x "$moddir/lkmloader" ]; then
    loader=lkmloader
    "$moddir/lkmloader" "$ko"
    rc=$?
  else
    echo "[load] no ksud or bundled lkmloader"
    return 1
  fi
  echo "[load] $loader rc=$rc $(date '+%m-%d %H:%M:%S')"
  if [ "$rc" -ne 0 ]; then
    echo "[load] module not loaded; see the loader error above"
    return 1
  fi
  # Touch and the mode are separate on purpose: a chmod failure must not read
  # as "not loaded" and provoke a second insmod over a live instance.
  touch "$marker"
  chmod 600 "$marker" 2>/dev/null || true
  return 0
}
