#!/bin/sh
set -e

clang_files=$(git ls-files '*.c' '*.h')
if [ -n "$clang_files" ]; then
  for f in $clang_files; do
    echo "Running clang-tidy on $f"
    clang-tidy -quiet "$f" --
  done
fi

make test
