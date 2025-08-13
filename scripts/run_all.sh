#!/bin/sh
set -eu
FAIL=0
for d in modules/*; do
  [ -d "$d" ] || continue
  echo "== $d =="
  (cd "$d" && sh scripts/test.sh) || FAIL=1
done
exit $FAIL
