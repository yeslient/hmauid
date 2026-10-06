# Tosya

*Тося.* Light grey hair cut at the ear, light green eyes with red pupils, the school uniform, the black
apron with the large pocket, the small red pin on the right side.

She is not installed so much as let in. The archive carries one build per kernel family; the installer
finds yours, leaves it in place as `ko/tosya.ko` and lets the rest go. When nothing matches, the closest
build is left as `ko/tosya.ko.try` and the installer says which family was wanted -- a kernel that is not
in the archive is named, never guessed at.

An install that carries a different id is a different module: remove it before this one is added, or the
device remembers both.

`post-fs-data.sh` brings her up early, `service.sh` tries again before the rule sync that keeps watch
beside the app data. Loading prefers `/data/adb/ksud insmod` where the device offers it, and falls back
to the loader that travels with the archive. What happened is written to `state/sync.log`.

Where the inline path is taken the syscall tables are left alone; where it is not, the log names the
mechanism that took over instead. The rest of this article lives on the repository front page.
