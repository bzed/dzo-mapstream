#!/bin/sh
# Builds build/@dzo-mapstream and build/@dzo-mapstream-tests.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build
rm -rf "$B"
mkdir -p "$B/@dzo-mapstream/addons" "$B/@dzo-mapstream-tests/addons"
cp "$ROOT/mod.cpp" "$B/@dzo-mapstream/mod.cpp"
makepbo -B -P -@=dzo_mapstream "$ROOT/dzo_mapstream" "$B/@dzo-mapstream/addons/dzo_mapstream.pbo" >"$B/makepbo-main.log" 2>&1 \
	|| { cat "$B/makepbo-main.log"; exit 1; }
makepbo -B -P -@=dzo_mapstream_tests "$ROOT/tests/dzo_mapstream_tests" "$B/@dzo-mapstream-tests/addons/dzo_mapstream_tests.pbo" >"$B/makepbo-tests.log" 2>&1 \
	|| { cat "$B/makepbo-tests.log"; exit 1; }
echo "built $B"
