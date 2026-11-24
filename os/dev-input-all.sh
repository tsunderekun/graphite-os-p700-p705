#!/bin/sh
# Capabilities of every input device (read-only, no key presses needed).
for ev in /dev/input/event*; do
    echo "===== $ev ====="
    timeout 1 evtest "$ev" 2>&1 | sed -n '1,25p'
done
