#!/bin/sh
set -e

clang_files=$(git ls-files '*.c' '*.h')
if [ -n "$clang_files" ]; then
  for f in $clang_files; do
    echo "Running clang-tidy on $f"
    # Run clang-tidy but keep going even if it fails
    clang-tidy -checks='*,-readability-identifier-length' -quiet "$f" -- || true
  done
fi

make test
