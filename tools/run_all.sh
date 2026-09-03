#!/bin/sh
# Regenerate clean/ from Runtime.exe.c + Runtime.exe.asm
set -e
cd "$(dirname "$0")/.."
mkdir -p clean
for s in parse asmstrings strings recover classify asmev clean emit; do
  echo "== $s"; python3 "tools/$s.py"
done
