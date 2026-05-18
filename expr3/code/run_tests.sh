#!/usr/bin/env bash
set -euo pipefail

make clean
make

echo "===== Basic Tests: 17 ====="
for f in ./test/test_[0-9][0-9]_*.cmm; do
  echo
  echo ">>> $f"
  ./cc "$f" || true
done

echo
echo "===== Optional Tests: 6 ====="
for f in ./test/test_optional_[0-9][0-9]_*.cmm; do
  echo
  echo ">>> $f"
  ./cc "$f" || true
done
