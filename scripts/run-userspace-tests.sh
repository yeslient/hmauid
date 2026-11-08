#!/usr/bin/env bash
# Real parsers/rules plus the production Syncer with a deterministic transport.
# Needs a C++23 compiler (GCC 14+), nlohmann-json3-dev and zlib1g-dev.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
out=build/tests/userspace
mkdir -p "$out"
flags=(-std=c++23 -O1 -g -Wall -Wextra -I src/tools -I src/include)
objects=()
for name in abx text_xml packages_xml packages rules preset_rules paths policy_plan apk_collector; do
  "$CXX" "${flags[@]}" -c "src/tools/$name.cpp" -o "$out/$name.o"
  objects+=("$out/$name.o")
done
for test in abx_reader packages_xml packages rules presets rule_sources policy_plan apk_collector; do
  "$CXX" "${flags[@]}" "scripts/${test}_test.cpp" "${objects[@]}" -lz -o "$out/$test"
  "$out/$test"
done
python3 scripts/test-sync-flow.py
