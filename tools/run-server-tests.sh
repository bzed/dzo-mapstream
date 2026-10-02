#!/bin/bash
# Headless server tests. Usage: tools/run-server-tests.sh [scenario...]   (default: all scenarios)
# Env: DZOMS_TREE (server tree, default ~/dayz-testserver-dzoms), DZOMS_RUNTIME (seconds, default 120)
SKILL=/home/bzed/.claude/skills/dayz-dev/scripts
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TREE=${DZOMS_TREE:-$HOME/dayz-testserver-dzoms}
RUNTIME=${DZOMS_RUNTIME:-120}
BUILD=$ROOT/build
PROF=$TREE/profiles
SCEN_DIR=$PROF/dzo-mapstream-tests
CFG_DIR=$PROF/dzo-mapstream
[ $# -eq 0 ] && set -- boot unit noconfig broken fixtures determinism

"$ROOT/tools/build.sh" >/dev/null || { echo "build failed"; exit 1; }
"$SKILL/make-server-tree.sh" "$TREE" >/dev/null || { echo "tree failed"; exit 1; }
WORKSHOP=$("$SKILL/find-dayzserver.sh" -w) || exit 1
ln -sfn "$WORKSHOP/2545327648" "$TREE/@DabsFramework"
ln -sfn "$BUILD/@dzo-mapstream" "$TREE/@dzo-mapstream"
ln -sfn "$BUILD/@dzo-mapstream-tests" "$TREE/@dzo-mapstream-tests"

# run_server <label> <mods> <ready-regex>; leaves the log at $BUILD/last-<label>.log
run_server() {
	local label=$1 mods=$2 ready=$3
	set -- $("$SKILL/free-ports.sh" 3)
	local game=$1 query=$2 rcon=$3
	sed -i -E 's/steamQueryPort *= *[0-9]+;//g' "$TREE/serverDZ.cfg"
	printf '\nsteamQueryPort = %s;\n' "$query" >>"$TREE/serverDZ.cfg"
	mkdir -p "$PROF/battleye"
	rm -f "$PROF"/battleye/beserver_x64_active_*.cfg
	printf 'RConPassword %s\nRestrictRCon 0\nRConPort %s\n' "$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c10)" "$rcon" >"$PROF/battleye/beserver_x64.cfg"
	rm -f "$PROF"/script_*.log "$PROF"/*.RPT "$PROF"/error.log
	(
		cd "$TREE" && ulimit -c 0 && exec timeout -k 15 "$RUNTIME" ./DayZServer -config=serverDZ.cfg -profiles=profiles \
			"-mod=$mods" -port="$game" -nosplash -nopause -dologs >"$BUILD/server-stdout.log" 2>&1
	) &
	local pid=$! waited=0 found=0
	while kill -0 "$pid" 2>/dev/null; do
		sleep 2
		waited=$((waited + 2))
		local log
		log=$(ls -t "$PROF"/script_*.log 2>/dev/null | head -1)
		if [ -n "$ready" ] && [ -n "$log" ] && grep -qE "$ready" "$log"; then found=1; sleep 2; break; fi
		[ "$waited" -ge "$RUNTIME" ] && break
	done
	local child
	child=$(pgrep -P "$pid" | head -1)
	kill "$pid" 2>/dev/null
	[ -n "$child" ] && kill "$child" 2>/dev/null
	for _ in 1 2 3 4 5 6 7 8; do kill -0 "$pid" 2>/dev/null || break; sleep 2; done
	[ -n "$child" ] && kill -9 "$child" 2>/dev/null
	kill -9 "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
	cat "$PROF"/script_*.log >"$BUILD/last-$label.log" 2>/dev/null
	[ "$found" = 1 ] || [ -z "$ready" ]
}

# module_count <log> <Game|World|Mission> -> prints the file count of the last matching Module line
module_count() {
	grep -h "Module: $2" "$1" | tail -1 | sed -E 's/.*loaded ([0-9]+)x? files.*/\1/'
}

scenario_prepare() {
	rm -rf "$SCEN_DIR"
	mkdir -p "$SCEN_DIR"
	echo "$1" >"$SCEN_DIR/scenario.txt"
	case "$1" in
	unit)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/maps"
		echo '{ "maps": [] }' >"$CFG_DIR/config.json"
		;;
	noconfig)
		rm -rf "$CFG_DIR"
		;;
	broken)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/maps"
		echo '{ "maps": [ { "file": "a.dze" } ' >"$CFG_DIR/config.json"
		cp "$CFG_DIR/config.json" "$SCEN_DIR/config.json.orig"
		;;
	fixtures)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/maps"
		write_fixture_config "$2"
		;;
	esac
}

# write_fixture_config <normal|reversed>: the maps themselves are written by the test addon (Prepare)
write_fixture_config() {
	local a='{ "file": "fixture_a.dze" }' b='{ "file": "fixture_b.dze", "networked": ["!Land_Container_1Bo"] }' maps
	maps="$a, $b"
	[ "$1" = reversed ] && maps="$b, $a"
	cat >"$CFG_DIR/config.json" <<JSON
{
    "maps": [ $maps ],
    "networked": [ "@doors", "@ladders", "@actions", "@fixedItems", "*Container*" ],
    "batchSize": 50,
    "debugLog": true
}
JSON
}

scenario_boot() {
	local base mod
	run_server boot-vanilla "@DabsFramework" "" || { echo "boot-vanilla: server did not run"; return 1; }
	run_server boot-mod "@DabsFramework;@dzo-mapstream" '\[dzo-mapstream\] ready' || { echo "boot: no ready line"; return 1; }
	base=$(module_count "$BUILD/last-boot-vanilla.log" Game)
	mod=$(module_count "$BUILD/last-boot-mod.log" Game)
	echo "Game module files: vanilla+Dabs $base, with mod $mod"
	if [ -z "$base" ] || [ -z "$mod" ] || [ "$mod" -le "$base" ]; then echo "mod scripts not loaded (Game)"; return 1; fi
	base=$(module_count "$BUILD/last-boot-vanilla.log" World)
	mod=$(module_count "$BUILD/last-boot-mod.log" World)
	echo "World module files: vanilla+Dabs $base, with mod $mod"
	if [ -z "$base" ] || [ -z "$mod" ] || [ "$mod" -le "$base" ]; then echo "mod scripts not loaded (World)"; return 1; fi
	base=$(module_count "$BUILD/last-boot-vanilla.log" Mission)
	mod=$(module_count "$BUILD/last-boot-mod.log" Mission)
	echo "Mission module files: vanilla+Dabs $base, with mod $mod"
	if [ -z "$base" ] || [ -z "$mod" ] || [ "$mod" -le "$base" ]; then echo "mod scripts not loaded (Mission)"; return 1; fi
	if grep -hE 'SCRIPT.*\(E\)' "$BUILD/last-boot-mod.log" | grep -v 'JSON ERROR'; then return 1; fi
	return 0
}

# scenario_default <log name> <scenario> [config variant]
scenario_default() {
	local name=$1 scenario=${2:-$1}
	scenario_prepare "$scenario" "$3"
	run_server "$name" "@DabsFramework;@dzo-mapstream;@dzo-mapstream-tests" '\[dzo-mapstream-test\] DONE' || { echo "$name: no DONE line"; grep -hE 'SCRIPT.*\(E\)|FAIL' "$BUILD/last-$name.log" | head -20; return 1; }
	local log=$BUILD/last-$name.log
	# the engine itself logs "(E): JSON ERROR" when the broken-config case is loaded on purpose
	if grep -hE 'SCRIPT.*\(E\)' "$log" | grep -v 'JSON ERROR' | head -20 | grep .; then return 1; fi
	grep -h 'dzo-mapstream-test\] FAIL' "$log"
	grep -q '\[dzo-mapstream-test\] DONE pass=[0-9]* fail=0$' "$log" || return 1
	case "$scenario" in
	noconfig) [ -f "$CFG_DIR/config.json" ] || { echo "default config.json was not written"; return 1; } ;;
	broken)
		cmp "$CFG_DIR/config.json" "$SCEN_DIR/config.json.orig" || { echo "broken config.json was modified"; return 1; }
		grep -q '\[dzo-mapstream\] ERROR: config error' "$log" || { echo "no config error logged"; return 1; }
		;;
	esac
	return 0
}

digest_of() {
	grep -h '\[dzo-mapstream\] ready: digest' "$BUILD/last-$1.log" | tail -1 | sed 's/.*digest //'
}

# fixtures config twice -> same digest; maps in reverse order -> a different one
scenario_determinism() {
	scenario_default determinism-1 fixtures normal || return 1
	scenario_default determinism-2 fixtures normal || return 1
	scenario_default determinism-3 fixtures reversed || return 1
	local d1 d2 d3
	d1=$(digest_of determinism-1)
	d2=$(digest_of determinism-2)
	d3=$(digest_of determinism-3)
	echo "digests: $d1 $d2 $d3"
	[ -n "$d1" ] && [ "$d1" = "$d2" ] || { echo "digest differs between identical runs"; return 1; }
	[ "$d1" != "$d3" ] || { echo "digest did not change with the map order"; return 1; }
	return 0
}

status=0
for s in "$@"; do
	case "$s" in
	boot) scenario_boot ;;
	determinism) scenario_determinism ;;
	fixtures) scenario_default fixtures fixtures normal ;;
	*) scenario_default "$s" ;;
	esac
	if [ $? -eq 0 ]; then
		echo "$s: ok $(grep -h 'DONE pass' "$BUILD/last-$s.log" 2>/dev/null | tail -1)"
	else
		echo "$s: FAILED"
		status=1
	fi
done
exit $status
