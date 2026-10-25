#!/bin/bash
# Real client check on Livonia: one .dze, the player stands at its densest rock cluster (A), 600 m away (B), at A
# again. The client probe (Test_RockView.c) logs every change around six rocks; this script takes screenshots
# every 3 s during the first 24 s of each A phase.
# Usage: tools/run-rock-view.sh <file.dze> <out-dir>     Env: DZOMS_TREE (default /tmp/dzoms-stable)
# Needs a running Steam, sway, grim (dayz-dev skill: testing/client-testing.md). One game at a time.
SKILL=/home/bzed/.claude/skills/dayz-dev/scripts
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DZE=${1:?usage: run-rock-view.sh <file.dze> <out-dir>}
OUT=${2:?usage: run-rock-view.sh <file.dze> <out-dir>}
T=${DZOMS_TREE:-/tmp/dzoms-stable}
C="$HOME/.steam/debian-installation/steamapps/common/DayZ"
# the bldr_* classes of the .dze files come from the Workshop mod BuilderItems (1565871491)
ED=$("$SKILL/find-dayzserver.sh" -w)/1565871491
MODS="@DabsFramework;@BuilderItems;@dzo-mapstream;@dzo-mapstream-tests"
"$SKILL/dayz-client-headless.sh" status >/dev/null || { echo "a DayZ client is running; stop it first"; exit 1; }
DZOMS_TREE=$T "$ROOT/tools/run-server-tests.sh" boot >/dev/null 2>&1 || { echo "build/boot failed"; exit 1; }
ln -sfn "$("$SKILL/find-dayzserver.sh" -w)/2545327648" "$C/@DabsFramework"
for m in @dzo-mapstream @dzo-mapstream-tests; do ln -sfn "$ROOT/build/$m" "$C/$m"; done
ln -sfn "$ED" "$C/@BuilderItems"; ln -sfn "$ED" "$T/@BuilderItems"
sed -i 's/^verifySignatures = 2;/verifySignatures = 0;/; s/template="dayzOffline\.[a-z]*"/template="dayzOffline.enoch"/' "$T/serverDZ.cfg"
grep -q "^BattlEye" "$T/serverDZ.cfg" || echo "BattlEye = 0;" >>"$T/serverDZ.cfg"
P=$T/profiles
rm -rf "$P/dzo-mapstream-tests" "$P/dzo"; mkdir -p "$P/dzo-mapstream-tests" "$P/dzo/mapstream/DZE"
echo rockview >"$P/dzo-mapstream-tests/scenario.txt"
cp "$DZE" "$P/dzo/mapstream/DZE/"
echo '{ "networked": ["@doors", "@ladders", "@actions", "@fixedItems"], "debugLog": true }' >"$P/dzo/mapstream/config.json"
rm -f "$P"/script_*.log
mkdir -p "$OUT"
"$SKILL/dayz-client-test.sh" -t "$T" -s "$MODS" -c "$MODS" -w 600 -d 'dzo-mapstream-test\] rock done' -k 'dzo-mapstream-test' -o "$OUT" >"$OUT.out" 2>&1 &
RUNNER=$!
seen=0
while kill -0 $RUNNER 2>/dev/null; do
	log=$(ls -t "$P"/script_*.log 2>/dev/null | head -1)
	n=$(grep -c "rock phase [02] " "$log" 2>/dev/null); n=${n:-0}
	if [ "$n" -gt "$seen" ]; then
		seen=$n
		for s in 0 1 2 3 4 5 6 7 8; do
			"$SKILL/dayz-client-headless.sh" shot "$OUT/client" "$OUT/A${n}_$((s * 3)).png" 2>/dev/null
			sleep 3
		done
	fi
	sleep 1
done
for m in @DabsFramework @BuilderItems @dzo-mapstream @dzo-mapstream-tests; do rm -f "$C/$m"; done; rm -f "$T/@BuilderItems"
sed -i 's/template="dayzOffline\.[a-z]*"/template="dayzOffline.chernarusplus"/' "$T/serverDZ.cfg"
tail -12 "$OUT.out"
