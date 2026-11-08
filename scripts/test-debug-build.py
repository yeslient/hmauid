#!/usr/bin/env python3
"""Exercise the real build switch and the production diagnostics function."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/main.c").read_text()
declaration = source[source.index("#ifdef TOSYA_DEBUG_ALWAYS"):
                     source.index("module_param_named(debug")]
start = source.index("void tosya_debug_init(bool on)")
function = source[start:source.index("\nstatic int __init", start)]

with tempfile.TemporaryDirectory(prefix="tosya-debug-") as temp:
    work = Path(temp)
    compiler = work / "ddk/clang/test/bin"
    compiler.mkdir(parents=True)
    kernel = work / "ddk/kdir/test"
    kernel.mkdir(parents=True)
    (kernel / "Makefile").touch()
    for name in ("clang", "llvm-strip"):
        path = compiler / name
        path.write_text("#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
    make = work / "make"
    make.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
args = sys.argv[1:]
pathlib.Path(os.environ["TOSYA_TEST_ARGS"]).write_text(json.dumps(args))
stage = next(a[2:] for a in args if a.startswith("M="))
(pathlib.Path(stage) / "tosya.ko").touch()
''')
    make.chmod(0o755)
    harness = work / "debug.c"
    harness.write_text('''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static int tosya_debug_key, tosya_debug_work;
static int enabled, scheduled;
static const char *message;
#define HZ 100
#define static_branch_enable(key) ((void)(key), ++enabled)
#define schedule_delayed_work(work, ticks) \
    ((void)(work), assert((ticks) == 60UL * HZ), ++scheduled)
#define pr_info(text) (message = (text))
''' + declaration + function + '''
int main(void) {
    assert(tosya_debug == EXPECT_ALWAYS);
    tosya_debug_init(false);
    assert(!enabled && !scheduled && !message);
    tosya_debug_init(true);
    assert(enabled == 1 && scheduled == !EXPECT_ALWAYS);
    assert(strstr(message, EXPECT_ALWAYS ?
        "compiled in, they never turn off" : "on for 60 s"));
}
''')
    for debug in (0, 1):
        args_file = work / "args.json"
        env = dict(os.environ, DDK_ROOT=str(work / "ddk"),
                   UG_KBUILD_MAKE=str(make), TOSYA_TEST_ARGS=str(args_file))
        subprocess.run(["bash", str(root / "scripts/build-kmi.sh"), "test",
                        "test", str(work / "stage"), str(work / "tosya.ko"),
                        str(debug)], env=env, check=True)
        args = json.loads(args_file.read_text())
        flags = [a.removeprefix("KCFLAGS=") for a in args
                 if a.startswith("KCFLAGS=")]
        assert flags == (["-DTOSYA_DEBUG_ALWAYS=1"] if debug else []), args
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=c11", f"-DEXPECT_ALWAYS={debug}"] +
                       [f for value in flags for f in shlex.split(value)] +
                       [str(harness), "-o", str(work / "test")], check=True)
        subprocess.run([str(work / "test")], check=True)

assert '"$<BOOL:${UG_DEBUG}>"' in (root / "CMakeLists.txt").read_text()
print("diagnostics: release parameter and permanent build modes passed")
