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

Filled in from the multi-user audit and the poll-cost quantification; the
kernel chains (policy apply, the directory and inline hooks, the netlink
dispatch) are documented in `docs/design.md` and are not repeated here.

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

### To be recorded in later passes

- Poll-cost quantification and the multi-user scope audit (measured on
  0.7.0+ci.32; entries land with the audit).
- The webview_zygote call-site invariants (WZ-1..3, with the tagging-gate
  boundary that acceptance item 4 turned into a lesson).
