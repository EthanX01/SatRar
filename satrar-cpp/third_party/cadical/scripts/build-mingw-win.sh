#!/usr/bin/env bash
set -euo pipefail
export PATH=/c/MinGW/bin:$PATH
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [ ! -f build/makefile ]; then
  ./configure
fi
cd build
../scripts/make-build-header.sh > build.hpp
CXXFLAGS="-Wall -Wextra -O3 -DNDEBUG -DNUNLOCKED"
for f in ../src/*.cpp; do
  b=$(basename "$f" .cpp)
  case "$b" in cadical|mobical) continue ;; esac
  echo "[cadical-build] $b"
  g++ $CXXFLAGS -I. -I../src -c "$f" -o "$b.o"
done
for f in ../contrib/*.cpp; do
  [ -f "$f" ] || continue
  b=$(basename "$f" .cpp)
  echo "[cadical-build] contrib/$b"
  g++ $CXXFLAGS -I. -I../src -c "$f" -o "$b.o"
done
ar rc libcadical.a *.o
g++ $CXXFLAGS -I. -I../src -o cadical.exe ../src/cadical.cpp -L. -lcadical -lpsapi
echo "[cadical-build] ok: $(pwd)/cadical.exe"
./cadical.exe --version