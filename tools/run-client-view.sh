#!/bin/bash
# Real client check: a headless DayZ client joins a server with tests/dze loaded; the server walks the player to
# 35 m in front of each object and this script takes a screenshot per stop (look at them yourself).
# Usage: tools/run-client-view.sh <out-dir>     Env: DZOMS_TREE (default /tmp/dzoms-stable), DZOMS_SERVER=exp for the experimental pair
# Needs a running Steam, sway, grim (dayz-dev skill: testing/client-testing.md). One game at a time.
SKILL=/home/bzed/.claude/skills/dayz-dev/scripts
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: run-client-view.sh <out-dir>}
T=${DZOMS_TREE:-/tmp/dzoms-stable}
EFLAG=; CDIR=DayZ
[ "${DZOMS_SERVER:-stable}" = exp ] && { EFLAG=-e; CDIR="DayZ Exp"; }
C="$HOME/.steam/debian-installation/steamapps/common/$CDIR"
MODS="@DabsFramework;@dzo-mapstream;@dzo-mapstream-tests"
"$SKILL/dayz-client-headless.sh" status >/dev/null || { echo "a DayZ client is running; stop it first"; exit 1; }
DZOMS_TREE=$T "$ROOT/tools/run-server-tests.sh" boot >/dev/null 2>&1 || { echo "build/boot failed"; exit 1; }
DABS="$ROOT/build/@DabsFramework"	# exp: the patched copy the test runner made
[ -z "$EFLAG" ] && DABS="$("$SKILL/find-dayzserver.sh" -w)/2545327648"
ln -sfn "$DABS" "$C/@DabsFramework"
for m in @dzo-mapstream @dzo-mapstream-tests; do ln -sfn "$ROOT/build/$m" "$C/$m"; done
sed -i 's/^verifySignatures = 2;/verifySignatures = 0;/' "$T/serverDZ.cfg"; grep -q "^BattlEye" "$T/serverDZ.cfg" || echo "BattlEye = 0;" >>"$T/serverDZ.cfg"
P=$T/profiles
rm -rf "$P/dzo-mapstream-tests" "$P/dzo"; mkdir -p "$P/dzo-mapstream-tests" "$P/dzo/mapstream/DZE"
echo clientview >"$P/dzo-mapstream-tests/scenario.txt"
cp "$ROOT/tests/dze/"*.dze "$P/dzo/mapstream/DZE/"
echo '{ "networked": ["@doors", "@ladders", "@actions", "@fixedItems"], "debugLog": true }' >"$P/dzo/mapstream/config.json"
rm -f "$P"/script_*.log
mkdir -p "$OUT"
"$SKILL/dayz-client-test.sh" $EFLAG -t "$T" -s "$MODS" -c "$MODS" -w 500 -d 'dzo-mapstream-test\] view done' -k 'dzo-mapstream-test' -o "$OUT" >"$OUT.out" 2>&1 &
RUNNER=$!
shot=0; seen=-1
while kill -0 $RUNNER 2>/dev/null; do
	log=$(ls -t "$P"/script_*.log 2>/dev/null | head -1)
	n=$(grep -c "view stop " "$log" 2>/dev/null); n=${n:-0}
	if [ "$n" -gt "$seen" ] && [ "$n" -gt 0 ]; then
		seen=$n
		sleep 20
		"$SKILL/dayz-client-headless.sh" shot "$OUT/client" "$OUT/stop$((n - 1)).png" 2>/dev/null
	fi
	sleep 2
done
for m in @DabsFramework @dzo-mapstream @dzo-mapstream-tests; do rm -f "$C/$m"; done
cat "$OUT.out" | tail -12
