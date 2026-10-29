#!/usr/bin/env bash
# Compile and link sync-tool for the host, with the two Android-only pieces
# stubbed: <sys/system_properties.h> from scripts/hosttest/sys, and the
# property calls it declares defined in scripts/hosttest/android_props_stub.cpp.
#
# The link is the point. The NDK build lives behind the DDK images, so a
# method declared in netlink.hpp but missing from netlink.cpp used to reach
# CI as a link error only after the eight kernel modules had built; here the
# linker rejects it in a minute, and an exhaustive check outranks any review.
#
# Needs: g++ (C++23), nlohmann-json3-dev, zlib1g-dev.
set -euo pipefail
cd "$(dirname "$0")/.."

out="${1:-build/tools-host/sync-tool-host}"
mkdir -p "$(dirname "$out")"

g++ -std=c++23 -Wall -Wextra \
	-I src/tools -I src/include -I scripts/hosttest \
	src/tools/abx.cpp src/tools/text_xml.cpp src/tools/packages_xml.cpp \
	src/tools/main.cpp src/tools/sync.cpp src/tools/watcher.cpp \
	src/tools/netlink.cpp src/tools/status.cpp src/tools/rules.cpp \
	src/tools/preset_rules.cpp src/tools/packages.cpp src/tools/paths.cpp \
	scripts/hosttest/android_props_stub.cpp \
	-o "$out" -ldl -lz

echo "host build: $out"
