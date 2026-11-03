#!/usr/bin/env bash
# The invariants the tree promises but a refactor could silently delete: each
# entry pairs a file with a marker that must be present in it. A rename or a
# "cleanup" that drops a marker fails here instead of rotting the promise.
# Docs/LIFECYCLE.md's invariants section carries the full statement each
# marker stands for.
set -eu
cd "$(dirname "$0")/.."

fail=0
while read -r file marker; do
	[ -n "$marker" ] || continue
	if ! grep -qF "$marker" "$file"; then
		echo "  missing invariant $marker in $file"
		fail=1
	fi
done <<'EOF'
src/inline_hooks.c WZ-1
src/lsm.c WZ-2
src/table_hooks.c WZ-3
EOF

if [ "$fail" -eq 0 ]; then
	echo "invariants: OK"
fi
exit "$fail"
