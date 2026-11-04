# Lifecycle

The module from install to removal: entry points, event chains, and per phase
the criterion that says how a *failure* in that phase is found -- a phase with
only success criteria is not a lifecycle, it is a brochure.

Anchors are written `file: symbol()`. `scripts/check_lifecycle_anchors.py`
greps each anchor's symbol in its file, so a rename fails the check instead of
letting the doc rot.

## 1. Install

What happens: the module manager unpacks the zip and runs `module/customize.sh`;
the first boot runs `post-fs-data.sh` (clears the boot-scoped loaded marker,
loads), then `service.sh` (kills any leftover daemon, rotates the log, loads if
post-fs-data did not, starts sync-tool).

- `module/customize.sh: kmi_from_uname()` reads the branch and the version out
  of `uname -r` separately -- the "11" in `android14-11` is a KMI generation,
  not a kernel version -- and copies `ko/<kmi>_arm64_tosya.ko` to
  `ko/tosya.ko`, deleting the rest. No matching ko: the closest build is kept
  as `tosya.ko.try` for explicit diagnosis, never auto-loaded; the loader can
  adjust vermagic, it cannot fix a layout mismatch.
- **Overwrite-install semantics**: an update replaces files only; the running
  daemon and the loaded ko keep running until reboot.
  `module/load.sh: tosya_load()` refuses a second insmod in the same boot via
  the
  `state/.loaded` marker ("already loaded this boot; replacing the module
  requires a reboot"), so a same-boot double load cannot stack.
- `module/load.sh: tosya_load()` picks the loader (`ksud` when present, else
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
- **Log rotation acceptance** (the threshold sits ~8 idle days out, so it is
  never triggered naturally -- it has to be forced once to count as verified):

      su -c 'head -c 5000000 /dev/zero >> /data/adb/modules/<id>/state/sync.log'
      reboot
      # then: state/sync.log.1 exists at ~5.1 MB, state/sync.log grows from 0,
      # and sync.log's first new line reads "[svc] orphans left: 0"

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

What happens: the module manager runs `module/uninstall.sh` with the
directory still in place, then deletes the directory. **There is no rmmod:**
the module takes itself off the kernel's module list (`src/main.c`), so
nothing can name it for removal -- the ko leaves with the reboot a removal
asks for. The script therefore covers only what can outlive the directory:
the sync-tool that `service.sh` started with `&`, and the state files.

- `module/uninstall.sh`: kills the daemon by exact /proc cmdline match
  (`pkill -f` is a substring ERE and would also hit anything merely mentioning
  the path), logs `[uninst] killed N, orphans left: M`, clears
  `state/.loaded` and `state/token`. `state/sync.log` is deliberately kept:
  after removal it is the only record of what the module did.
- **How a remove failure is found**: a /proc cmdline **exactly equal** to
  `<moddir>/sync-tool ` after the script -- an exact match is the failure
  signal. A substring grep would count the checker's own shell (and the
  uninstaller itself) as an orphan of its own crime; the scripts' leftover
  count uses the same exact match as the kill for that reason.

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

- **How a run failure is found** (the kernel's `injected` line is a pr_info
  and never lands in sync.log -- a criterion that greps for it there condemns
  a healthy device):
  1. `state/sync.log` ends with `registered N app apk(s), X change(s) to send`,
     where N matches the status line's `apk N` -- and N itself is a live
     number: the count of packages `pm list packages` reports whose codePath
     resolves, not a memorized figure from some device;
  2. timestamps in sync.log grow; and installing or removing one app produces
     a fresh `registered` line with a changed N -- that line is the "alive"
     proof, not the boot-time one.
- **Queue overflow** (`src/tools/watcher.cpp: handle_inotify_events()`): an
  `IN_Q_OVERFLOW` is treated as interesting and re-arms the watches -- the
  full resync it triggers is the correct answer to dropped events, never
  "no event".
- **rmmod does not exist and that is fine**: a netlink command in flight when
  the module leaves gets ECONNREFUSED from a family that is gone -- a clean
  userspace-visible error, not a kernel fault; the daemon is killed by
  `module/uninstall.sh` before the directory goes anyway.

### The g_hash guess window (recorded with its cost, not implemented)

`src/policy.c: g_hash` starts at the assumed `__uidhashfn` bits=7 shape and is
re-detected at every apply (`detect_uid_hash()` reads the formula out of the
running kernel's own `find_user()`). Between a uid change and the next apply,
userspace can see the stale guess -- what that means concretely: a hidden
target's replacement may hash to a different bucket than the kernel's own
absent-uid walk would, so a cache-timing observer comparing the module's
probed line against the kernel's line sees a mismatch for at most one policy
generation. The window closes on the first apply after boot (the sync pushes
the policy seconds after the module loads), and a wrong guess only degrades
the indirection, never the hiding itself. Making the hash a published
snapshot instead of a global is a hardening suggestion; the cost is a
re-verification of every hash-touching path, and the benefit is a window that
in practice closes before any policy exists to observe.

### The apk record array is bounded by ever-installed, recycled lazily

`src/inode_hook.c: shadow_arm()` parks records instead of freeing them (an
inode may still point at the fops table a record made), so `g_shadow[]` is
bounded by TOSYA_APK_MAX *records ever installed*, not live ones, and the
memory is given back at unload. Records whose inode was reclaimed leave at
the next stream end (`tosya_apk_stream_finish()`); records never armed and
never reclaimed stay parked until then. On a device that installs and
reinstalls apps heavily for months the array fills and `shadow_arm` refuses
with `-ENOSPC` and a `pr_warn_once` -- the recovery is a reboot, which a
module update asks for anyway. Cost accepted, not an oversight.

### Rotation lives between the kill and the start, and only there

`module/service.sh` rotates `sync.log` after killing a leftover daemon and
before starting a new one -- nowhere else. The running sync-tool holds the
log fd: an `mv` while it lives leaves it writing into the renamed inode, the
fresh `sync.log` stays empty until reboot and the rotated copy grows forever.
The comment in the script says the same thing; this entry exists so the next
person who "simplifies" the script into a loop finds out here first.

### The exit path is dead by construction

`src/main.c: tosya_init()` ends with `kobject_del()` plus
`list_del(&THIS_MODULE->list)` -- the module takes itself off the kernel's
module list, and the comment there explains why it must stay the last thing
init does. The consequence every teardown concern hangs from: `delete_module`
finds its target only on that list, so **`rmmod` cannot name this module and
`tosya_exit()` never runs** -- the unload path is unreachable, not merely
unused. (The ko still leaves with a reboot, which every update asks for.)

That reachability fact bounds a real kernel-semantics gap one level down: the
table and LSM tier removes restore their slots and then wait a grace period
(`src/table_hooks.c: uid_tables_remove()`, `src/lsm.c` both variants -- the
table side goes through `tosya_patch_text`, i.e. stop_machine). stop_machine
stops CPUs, not tasks, and syscall dispatch holds no RCU read lock: on a
`CONFIG_PREEMPT_RCU` kernel a task preempted inside `uid_change_hook` does
not block the grace period, so a grace period after unpatch proves nothing.
On `PREEMPT_NONE/VOLUNTARY` the running task itself is the RCU read side and
the wait holds. Since the exit is unreachable, this is recorded, not fixed.

**Invalidation condition**: if the self-unlink in `tosya_init()` is ever
removed -- typically for debugging -- the table and LSM tier removes must
wait for `synchronize_rcu_tasks()` (resolved through `tosya_sym`, it is not
exported everywhere) before returning, or refuse to unload and demand a
reboot. Until then this entry, not a patch, is the guard.

Two adjacent claims adjudicated against the same path:

- The apk records freed at remove with unresolved inodes
  (`src/inode_hook.c: tosya_apk_remove()`) are safe **independently** of the
  unreachability: unresolved means either the record never armed
  (`orig_fops == NULL`, nothing points at its table) or `ilookup()` could not
  find the inode -- and an inode carrying our `i_fop` stays in the inode hash
  until eviction, because any dentry holding it holds a reference, so a
  still-live `i_fop` is always still resolvable and gets restored. An open
  file pins the module through `owner` regardless.
- `src/tiers.c: tosya_tier_revert_all()` has no caller in the kernel side on
  purpose: it is the symmetric teardown the tiers host test drives three
  times (`scripts/tiers_test.c`), not dead code.

### Deferred, with the measurement that would reopen them

- **dir-side empty-state static gate**: same shape as the find_user gate
  (`tosya_active_key`), deferred until the `statloop` instrument shows the
  empty-policy cost is real. The quantification contract: stat calls/second
  through the guarded parents, worst-path p99 per lookup, and the delta
  between armed and disarmed -- no gate without those numbers.
- **KAUX_STAGED_BYTES 2 MiB -> 1 MiB**: waits for the measured peak staging
  usage; the shrink is only worth a protocol touch if the peak says so.
- **Rebuilding the dir-guard host test**: `extract_dir.py` +
  `dir_host_test.c` existed only in a scratch tree and are gone; the suite
  runs complete again, and the guard's coverage is device acceptance until
  the ASan harness is rebuilt from `src/dir_hook.c`'s `TOSYA_DIR_TEST`
  blocks.
