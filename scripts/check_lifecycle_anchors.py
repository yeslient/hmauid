#!/usr/bin/env python3
"""Check that every anchor in docs/LIFECYCLE.md names a symbol that still exists.

Anchors are written `path: symbol()` inside backticks. A line-number drift is
not an error by design -- a rename is: the doc must not point at ghosts.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
doc = (root / 'docs/LIFECYCLE.md').read_text(encoding='utf-8')

anchors = set(re.findall(
    r'`([^\s`]+?\.(?:c|cpp|h|hpp|py|sh)):\s*([A-Za-z_][A-Za-z0-9_:]*)\(\)`',
    doc))
if not anchors:
    print('  no anchors found in docs/LIFECYCLE.md')
    sys.exit(1)

fail = 0
for file, symbol in sorted(anchors):
    path = root / file
    if not path.is_file():
        print(f'  missing file: {file}')
        fail = 1
        continue
    if f'{symbol}(' not in path.read_text(encoding='utf-8'):
        print(f'  renamed or gone: {symbol}() not in {file}')
        fail = 1
if fail == 0:
    print(f'lifecycle anchors: {len(anchors)} OK')
sys.exit(fail)
