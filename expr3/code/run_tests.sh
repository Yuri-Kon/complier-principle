#!/usr/bin/env bash
set -euo pipefail

make clean
make

echo "===== Project 3 Tests ====="
for f in ./test/test*.cmm; do
  echo
  echo ">>> $f"
  base="$(basename "${f%.cmm}")"
  out="./test/${base}.ir"
  ./cc "$f" "$out" || true
  if [[ -f "$out" ]]; then
    sed -n '1,200p' "$out"
  fi
done
