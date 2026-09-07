#!/bin/sh
# snapshot tests: run each fixture through peek and byte-compare with .out
cd "$(dirname "$0")/.."
BIN=$(find build -type f -name peek -perm -u+x -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 1 | cut -d' ' -f2-)
[ -x "$BIN" ] || { echo "peek binary not found — run xmake first"; exit 1; }
pass=0 fail=0
for f in tests/cases/*.html; do
    exp="${f%.html}.out"
    got=$(mktemp)
    "$BIN" "$f" > "$got" 2>&1
    if cmp -s "$exp" "$got"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL $f"
        diff "$exp" "$got" | head -20
    fi
    rm -f "$got"
done
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
