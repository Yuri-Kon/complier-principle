#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$ROOT_DIR/test"
OUTPUT_DIR="${1:-$TEST_DIR/output}"

if [[ ! -d "$TEST_DIR" ]]; then
  echo "Test directory not found: $TEST_DIR" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

echo "[1/2] Building project..."
make --directory "$ROOT_DIR" clean >/dev/null
make --directory "$ROOT_DIR" >/dev/null

echo "[2/2] Running tests from $TEST_DIR"

total=0
passed=0

for test_file in "$TEST_DIR"/*.cmm; do
  [[ -e "$test_file" ]] || continue

  total=$((total + 1))
  test_name="$(basename "$test_file")"
  output_file="$OUTPUT_DIR/${test_name%.cmm}.out"

  if "$ROOT_DIR/cc" "$test_file" >"$output_file" 2>&1; then
    passed=$((passed + 1))
    printf '[PASS] %s -> %s\n' "$test_name" "$output_file"
  else
    printf '[FAIL] %s -> %s\n' "$test_name" "$output_file"
  fi
done

echo
printf 'Completed %d tests. Command succeeded for %d/%d cases.\n' "$total" "$passed" "$total"
echo "Outputs written to: $OUTPUT_DIR"
