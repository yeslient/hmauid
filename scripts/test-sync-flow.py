#!/usr/bin/env python3
"""Run production Syncer against relocated files and a deterministic transport.

Only private visibility and Android filesystem roots change in the test copy.
The production orchestration and APK state machine are compiled unchanged.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess

root = Path(__file__).resolve().parents[1]
out = Path(os.environ.get("TOSYA_SYNC_TEST_OUT", root / "build/tests/sync")).resolve()
out.mkdir(parents=True, exist_ok=True)
fixtures = out / "fixtures"
fixtures.mkdir(exist_ok=True)


def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


for path in (root / "src/tools").glob("*.hpp"):
    text = path.read_text()
    if path.name == "sync.hpp":
        text = text.replace("private:", "public:")
        text = replace_once(text, "RuleSource::known()",
                            '{RuleSource{' + json.dumps(str(fixtures / "config.json")) +
                            ', Tool::HmaOss}}')
    if path.name == "paths.hpp":
        text = replace_once(text, '"/data/system/packages.xml"',
                            json.dumps(str(fixtures / "packages.xml")))
    (out / path.name).write_text(text)

sources = []
for name in ("sync", "packages", "rules", "preset_rules", "paths", "abx",
             "text_xml", "packages_xml", "policy_plan", "apk_collector",
             "apk_publisher"):
    path = root / f"src/tools/{name}.cpp"
    if not path.exists():
        continue  # The same harness also runs before the extraction.
    text = path.read_text()
    if name == "sync":
        text = replace_once(text, '"/data/app"',
                            json.dumps(str(fixtures / "app")))
    if name == "packages":
        text = replace_once(text, '"/data/system/users"',
                            json.dumps(str(fixtures / "users")))
    copy = out / path.name
    copy.write_text(text)
    sources.append(str(copy))

command = shlex.split(os.environ.get("CXX", "g++")) + [
    "-std=c++23", "-O1", "-g", "-Wall", "-Wextra", "-ffunction-sections",
    "-fdata-sections", "-I", str(out), "-I", str(root / "src/include"),
    "-DTOSYA_TEST_ROOT=" + json.dumps(str(fixtures)),
] + shlex.split(os.environ.get("TOSYA_TEST_CXXFLAGS", ""))
subprocess.run(command + sources + [str(root / "scripts/sync_flow_test.cpp"),
               "-Wl,--gc-sections", "-lz", "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True, timeout=60)
