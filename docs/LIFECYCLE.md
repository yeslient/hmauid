# Lifecycle

The module from install to removal: entry points, event chains, and per phase
the criterion that says how a *failure* in that phase is found -- a phase with
only success criteria is not a lifecycle, it is a brochure.

Anchors are written `file: symbol()`. `scripts/check_lifecycle_anchors.py`
greps each anchor's symbol in its file, so a rename fails the check instead of
letting the doc rot.

## 1. Install

What happens: the module manager unpacks the zip and runs `module/ customize.sh`;
the first boot runs `post-fs-data.sh` (clears the boot-scoped loaded marker,
loads), then `service.sh` (kills any leftover daemon, rotates the log, loads if
post-fs-data did not, starts sync-tool).

- `module/ customize.sh: kmi_from_uname()` reads the branch and the version out
  of `uname -r` separately -- the "11" in `android14-11` is a KMI generation,
  not a kernel version -- and copies `ko/<kmi>_arm64_tosya.ko` to
  `ko/tosya.ko`, deleting the rest. No matching ko: the closest build is kept
  as `tosya.ko.try` for explicit diagnosis, never auto-loaded; the loader can
  adjust vermagic, it cannot fix a layout mismatch.
- **Overwrite-install semantics**: an update replaces files only; the running
  daemon and the loaded ko keep running until reboot. `module/ load.sh
  tosya_load()` refuses a second insmod in the same boot via the
  `state/.loaded` marker ("already loaded this boot; replacing the module
  requires a reboot"), so a same-boot double load cannot stack.
- `module/ load.sh tosya_load()` picks the loader (`ksud` when present, else
  the bundled `lkmloader`), logs `[load] <loader> rc=<rc>` either way, and on
  failure logs `[load] module not loaded; see the loader error above`.
- **How an install failure is found** (any one of three, the dmesg one carries
  a time limit because the ring is ~830 s and the boot lines are gone within
  hours):
  1. `state/.loaded` exists (this boot loaded once);
  2. `state/sync.log` first line reads `Loaded kernel module: .../tosya.ko`;
  3. `dmesg | grep 'tosya: injected'` **within ~5 minutes of boot**.
- **Bootloop recovery**: KernelSU safe mode (volume-key) and the `disable`
  file in the module directory both make the manager skip `service.sh`
  entirely, so nothing loads and nothing starts; verified once by creating the
  `disable` file and rebooting (see the acceptance log below).

## 2. Run

The kernel chains (policy apply, the directory and inline hooks, the netlink
dispatch) are documented in `docs/design.md` and are not repeated here. This
section records what the lifecycle audit measured on the device or read out of
the tool.

### Daemon idle cost (measured on 0.7.0+ci.32, screen-on steady state)

- 60 s `utime+stime` delta of the sync-tool process: 1 tick (CLK_TCK=100) =
  10 ms = **0.017% of one core**; voluntary context switches ~2/s (the 1 s
  poll tick plus inotify wakes).
- Process lifetime at 6.2 h uptime: 176 ticks = 1.76 s = 0.0079% of one core,
  **including every screen-off period in that window** -- the watcher's
  timerfds are CLOCK_MONOTONIC (`src/tools/watcher.cpp: Watcher::open()`),
  which does not advance in suspend, so the lifetime figure is an upper bound
  across all power states.
- Decision, recorded so it is not re-litigated: raising the post-unlock poll
  timeout from 1 s to 10 s was proposed as a power win; the measured saving
  (~0.16 s of CPU per 6 h) is below the 1-tick noise floor, and the change
  would buy a four-state timeout function, a wider CE-close detection window
  and a hosttest state machine against it. **Quantified as not worth it.**
  Re-open threshold: a 60 s delta of 6 ticks or more (0.1% of one core).

### Multi-user scope (audited in the tree, verified on the device)

The chain is user-aware end to end; the only user-0 artifact is the HMA config
itself, which is where the HMA app keeps it:

- `src/tools/packages.cpp: android_users()` enumerates `/data/system/users` --
  every user on the device, not just user 0.
- `src/tools/packages.cpp: expand_users()` multiplies every base pair into
  every enumerated user's uid range, with explicit u64 overflow guards.
- `src/tools/sync.cpp` re-resolves the user set when `/data/system/users`
  changes (the watcher watches that directory) and refreshes the kernel's name
  records when the user count changes.
- The kernel matches callers by app id (`src/policy.c: build_hiders()`), so a
  rule's caller and target apply in every user at once.
- **Device verification** (0.7.0+ci.32, users {0, 999:XSpace}): the config
  expands to 3127 base pairs and the kernel reports `injected 6254 pair(s),
  76 caller(s)` -- exactly 2x, the two enumerated users; `--list` answers
  identically for the user-0 and the user-999 uid of the same package, because
  the caller identity folds to the app id.

Scope notes: target uids are multiplied into every enumerated user whether or
not the package is installed there -- hiding is by uid, deterministic, and a
uid range with no processes behind it costs nothing. A package installed only
in a secondary user is covered as long as it is in the shared
`/data/system/packages.xml`; per-user install state does not gate pair
generation. The config is read from user 0 only
(`src/tools/paths.cpp: RuleSource::known()`).

## 3. Remove

What happens: the module manager runs `module/ uninstall.sh` with the
directory still in place, then deletes the directory. **There is no rmmod:**
the module takes itself off the kernel's module list (`src/main.c`), so
nothing can name it for removal -- the ko leaves with the reboot a removal
asks for. The script therefore covers only what can outlive the directory:
the sync-tool that `service.sh` started with `&`, and the state files.

- `module/ uninstall.sh`: kills the daemon by exact /proc cmdline match
  (`pkill -f` is a substring ERE and would also hit anything merely mentioning
  the path), logs `[uninst] killed N, orphans left: M`, clears
  `state/.loaded` and `state/token`. `state/sync.log` is deliberately kept:
  after removal it is the only record of what the module did.
- **How a remove failure is found**: `pgrep -f sync-tool` non-empty after the
  script -- an orphan is the failure signal itself.

## 4. Known boundaries

Every entry carries a proof, a cost, or an invalidation condition -- "not
fixed" without one of those is a suggestion, not a boundary.

### The apk index under ENOMEM (fail-closed by construction)

Reported externally as B5: "an apk-index ENOMEM can leave a use-after-free;
only reachable under memory pressure" -- and memory pressure is not an exotic
path on a low-RAM device. Adjudicated against the current tree: every
allocation failure on the apk-index path already fails closed, so there is
nothing to fix and the report is recorded as a proof instead.

- `src/inode_hook.c: index_new()` allocates the table and its slots in one
  kvzalloc; a block freed as a whole cannot leave half a table behind.
- `src/inode_hook.c: index_rebuild_locked()` returns on `index_new()` NULL
  with the **old table still published** -- readers never see a partial build.
  Placement exhaustion pr_warn_once's and keeps the old table too.
- `src/inode_hook.c: shadow_arm()` rolls the record back out of the array on
  `index_put()` failure (`g_shadow_n--`, kfree, -ENOSPC), so the record array
  and the index never disagree about what exists.
- `src/inode_hook.c: tosya_apk_shadow_sweep()` falls back to the locked scan
  when the key snapshot cannot be allocated, with `shadow_apply()` told it
  already holds the lock.
- Publication is one `rcu_assign_pointer()`; the replaced table goes through
  `kvfree_rcu()` only after the swap.

**Invalidation condition**: if `index_rebuild_locked()`'s NULL return ever
changes to releasing the old table before bailing out, or the single
allocation is split into table + slots, this proof expires and the boundary
must be re-adjudicated.

### Invariants (WZ: the tagging gates and the webview_zygote boundary)

Three call-site families hand uid transitions to `src/tag.c: tosya_tag_note()`,
each through a gate of the same shape (`before == 0 || before is an app uid`).
The gate ignores a **webview_zygote birth on purpose**: a helper zygote forks
at uid 1053 and then setresuids into the 90xxx isolated range, and that move's
before side is neither 0 nor an app uid -- so it never reaches the tag note.
That is the design, not a gap: an isolated child's identity comes from the apk
it opens (`src/inode_hook.c: tosya_shadow_open()`), not from a birth tag, and
a WebView renderer used as an attribution test subject produces **no signal by
construction** -- acceptance item 4 once read that silence as a defect.

- `WZ-1` -- `src/inline_hooks.c: tosya_setuid_inline_hook()`, the inline
  setuid hook's gate.
- `WZ-2` -- `src/lsm.c: tosya_fix_setuid()`, the LSM hook's gate; the file
  carries two kernel-version variants (< 6.12 and >= 6.12) and both carry the
  marker.
- `WZ-3` -- `src/table_hooks.c: uid_change_hook()`, the syscall-table hook's
  gate.

Guarded two ways: `scripts/check-invariants.sh` greps for the markers (wired
into CI and the host suite -- a dropped marker fails the check, not the
promise), and debug builds (`TOSYA_DEBUG_ON()`) warn inside
`src/tag.c: tosya_tag_note()` if a transition whose before side is neither 0
nor an app uid reaches it -- the signature of a gate that moved without the
contract moving with it. Comments carry the *why*; the script and the warn
carry the *must not break*.

### To be recorded in later passes

- The boundary-table entries still owed by the final pass: g_hash snapshot
  window, netlink in-flight vs rmmod, the apk array's bounded-lazy recycling,
  the rotation placement rule.
