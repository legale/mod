#!/bin/sh
# posix sh test driver
set -eu
mkdir -p out
make clean >/dev/null
make debug
make test || true
UNIT_RC=$?
make tsan
make test-thread || true
THR_RC=$?
make debug
make test-stress || true
STR_RC=$?
echo "unit=$UNIT_RC thread=$THR_RC stress=$STR_RC"
[ "$UNIT_RC" -eq 0 ] && [ "$THR_RC" -eq 0 ] && [ "$STR_RC" -eq 0 ]
