# Build

Use Linux with CMake 3.22+, GNU make, Python 3, an Android NDK, and the matching
DDK kernel tree/compiler for each KMI. Install `nlohmann-json3-dev` and `zlib1g-dev`
for the userspace helper. Initialize the pinned loader before configuring:

```sh
git submodule update --init --recursive
```

Each KMI builds with its DDK compiler so that Clang CFI and shadow call stack
instrumentation match. Kbuild links the module and writes its objects into
`build/kmi/<KMI>`, a directory holding symlinks to `src/`.

## Package build

With all default DDKs under `/opt/ddk`:

```sh
export ANDROID_NDK_HOME=/absolute/path/to/android-ndk
cmake -S . -B build -DUG_NDK="$ANDROID_NDK_HOME" -DDDK_ROOT=/opt/ddk
cmake --build build -j8
```

The default KMIs are android14-6.1, android15-6.6, android16-6.12 and android17-6.18.
`-DUG_ENABLE_5X=ON` adds the four legacy 5.x KMIs when their DDK trees are present.
Modules go to `build/ko`; the installable zip goes to `build/dist`. The package
also contains `sync-tool`, the pinned `external/lkmloader`, and the boot scripts.

Version metadata comes from git, falling back to `module/module.prop` for source
exports. `-DUG_VERSION_OVERRIDE=0.4.0-dev+local -DUG_VERSION_CODE_OVERRIDE=4000`
sets explicit metadata without changing tags or module attribution.

## Separate KMI builds

For environments with one DDK each, CMake and CI share `scripts/build-kmi.sh`:

| KMI | DDK compiler |
| --- | --- |
| android14-6.1 | clang-r487747c |
| android15-6.6 | clang-r510928 |
| android16-6.12 | clang-r536225 |
| android17-6.18 | clang-r584948c |

For example, run this inside the 6.1 build environment:

```sh
UG_KBUILD_JOBS=8 bash scripts/build-kmi.sh \
  android14-6.1 clang-r487747c \
  build/kmi/android14-6.1 build/ko/android14-6.1_arm64_tosya.ko
```

Collect the desired modules in `build/ko`, then package them with the NDK:

```sh
cmake -S . -B build -DUG_NDK="$ANDROID_NDK_HOME" -DUG_USE_PREBUILT_KO=ON
cmake --build build -j8
```

Prebuilt mode packages the files present in `build/ko`; keep that directory limited
to the intended candidate. `UG_DEBUG=ON` enables permanent kernel diagnostics.
Normal packages leave it off; the `debug=1` module parameter enables them for 60 seconds.

## Host verification and candidate gates

Use GCC 14 or newer for C++23 `<print>`, plus Clang, Python 3,
`nlohmann-json3-dev` and `zlib1g-dev`:

```sh
CXX=g++-14 bash scripts/build-host.sh
bash scripts/run-hosttest.sh
CXX=g++-14 bash scripts/run-userspace-tests.sh
python3 scripts/test-netlink-flow.py
bash scripts/check-format.sh
```

`run-userspace-tests.sh` runs the real package parsers, rules, presets and source
selection tests, then policy-plan and APK publication regressions. The sync-flow
harness copies production sources into `build/tests/sync`: only Android file roots
and private visibility are changed. It links a deterministic netlink boundary to
exercise lost replies, refusals, recovery and publication order without a device.
Run the same production flow under sanitizers with:

```sh
CXX=g++-14 TOSYA_TEST_CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  python3 scripts/test-sync-flow.py
```

`Syncer` owns input caches, scheduling, tokens and presentation. `policy_plan.cpp`
computes desired rules/names/callers from loaded inputs; `apk_collector.cpp`
resolves APK files; `apk_publisher.cpp` owns the uncertain publication ledger.
The collector's 10000-file cap and the kernel's allocated-record cap are separate.

The candidate ZIP job requires all eight ordinary KMI builds, the host build and
userspace regressions, the host suite and a permanent-diagnostics 6.1 build. Its
artifact includes the ZIP, `SHA256SUMS` and `verification.txt` with the source
commit and the pending device-acceptance boundary. These gates run on the checked
out CI commit; a local commit cannot be validated by dispatching an older remote
ref. The tag-driven Telegram release path is separate from this candidate job.

Device acceptance remains separate: install/reboot, verify policy and isolated
process behavior, and inspect diagnostics on the matching diagnostic build. A
permanent build logs `diagnostics on (compiled in, they never turn off)` when
enabled; it must still produce diagnostics after 60 seconds. A host test or a
string in a compiled module alone does not prove that device behavior.

## Loading

Install the zip through KernelSU and reboot. `customize.sh` selects the matching
KMI. `post-fs-data.sh` loads early; `service.sh` retries if needed and starts
`sync-tool`. Both prefer `/data/adb/ksud insmod` and use the bundled loader only
when ksud is absent. Logs are in the installed module's `state/sync.log`.

For manual loading from a root shell on a boot without this module active:

```sh
/data/adb/ksud insmod /data/local/tmp/tosya.ko 'uid_tier=inline setuid_tier=inline'
```

Forcing the inline tiers disables fallback for that attempt. Normal package loading
uses automatic selection; check module status for the installed mechanisms and
errors. Published inline hooks remain loaded until reboot, so updating package
files does not replace the running module. A vermagic release-string difference
alone does not establish incompatibility, but the loader cannot repair incompatible
KMI structure layouts.

## uidbench

This optional measurement tool is not a package component. Cross-compile it and
run as an app uid with hiding rules:

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android34-clang" \
  -O2 -static -o uidbench src/tools/uidbench.c
```

It samples hidden, absent and unhooked cases in one round. Compare latency on a
controlled device workload; functional success alone does not establish a speedup.
