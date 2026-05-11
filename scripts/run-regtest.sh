#!/usr/bin/env bash
set -euo pipefail
RUNS="${1:-3}"
EXPECTED=".github/macos-26-expected.txt"
FLAKY=".github/flaky-tests.txt"
PASS_COUNT=0

[[ -f "$EXPECTED" ]] || { echo "ERROR: $EXPECTED not found. Run from repo root." >&2; exit 1; }

for i in $(seq 1 "$RUNS"); do
  echo "=== Run $i / $RUNS ==="
  RAW="regtest-run${i}-raw.txt"
  FILTERED="regtest-run${i}-filtered.txt"

  XMLLINT=does_not_exist make check 2>&1 | tail -3
  XMLLINT=does_not_exist make regtest 2>&1 | tee "$RAW" | tail -5 || true

  awk '/^== [0-9]+ tests, .* ==$/{flag=1;next}/^$/{flag=0}flag' \
    < "$RAW" > "regtest-run${i}-all-failures.txt"

  > "$FILTERED"
  while IFS= read -r line; do
    TEST_NAME=$(echo "$line" | cut -f1 -d' ')
    grep -qF "$TEST_NAME" "$FLAKY" 2>/dev/null \
      && echo "  [flaky] $TEST_NAME" \
      || echo "$line" >> "$FILTERED"
  done < "regtest-run${i}-all-failures.txt"

  if diff -U 0 "$EXPECTED" "$FILTERED" > "regtest-run${i}-diff.txt" 2>&1; then
    echo "  ✓ Run $i matches expected."; PASS_COUNT=$(( PASS_COUNT + 1 ))
  else
    echo "  ✗ Run $i DIFFERS:"; cat "regtest-run${i}-diff.txt"
  fi
done

echo "=== Summary: $PASS_COUNT / $RUNS runs matched ==="
[[ "$PASS_COUNT" -eq "$RUNS" ]] && echo "Baseline established." && exit 0
echo "FAIL: not all runs matched."; exit 1
