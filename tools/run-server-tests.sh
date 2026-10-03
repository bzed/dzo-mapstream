#!/bin/bash
# Headless server tests. Usage: tools/run-server-tests.sh [scenario...]   (default: all scenarios)
# Env: DZOMS_SERVER (stable|exp, default stable: 1.29 stable or 1.30 experimental server),
#      DZOMS_TREE (server tree, default ~/dayz-testserver-dzoms[-exp]), DZOMS_RUNTIME (seconds, default 120)
SKILL=/home/bzed/.claude/skills/dayz-dev/scripts
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ "${DZOMS_SERVER:-stable}" = exp ]; then
	TREE=${DZOMS_TREE:-$HOME/dayz-testserver-dzoms-exp}
	STEAM_SERVER=$("$SKILL/find-dayzserver.sh" -e) || { echo "experimental server not installed"; exit 1; }
else
	TREE=${DZOMS_TREE:-$HOME/dayz-testserver-dzoms}
	STEAM_SERVER=$("$SKILL/find-dayzserver.sh") || { echo "stable server not installed"; exit 1; }
fi
RUNTIME=${DZOMS_RUNTIME:-120}
BUILD=$ROOT/build
PROF=$TREE/profiles
SCEN_DIR=$PROF/dzo-mapstream-tests
CFG_DIR=$PROF/dzo-mapstream
[ $# -eq 0 ] && set -- boot unit noconfig broken realdze fixtures determinism

"$ROOT/tools/build.sh" >/dev/null || { echo "build failed"; exit 1; }
"$SKILL/make-server-tree.sh" "$TREE" "$STEAM_SERVER" >/dev/null || { echo "tree failed"; exit 1; }
WORKSHOP=$("$SKILL/find-dayzserver.sh" -w) || exit 1
if [ "${DZOMS_SERVER:-stable}" = exp ]; then
	# Dabs Framework (workshop build 2026-05) has a typedef without ';' that the 1.30 compiler rejects
	# (types.c:52). Test-only copy with that one character added; the Steam files stay untouched.
	PATCHED=$BUILD/@DabsFramework
	rm -rf "$PATCHED" "$BUILD/dabs-src"
	mkdir -p "$PATCHED/addons" "$BUILD/dabs-src"
	cp "$WORKSHOP/2545327648/addons/"* "$PATCHED/addons/" || exit 1
	rm -f "$PATCHED"/addons/Scripts.pbo*
	armake2 unpack "$WORKSHOP/2545327648/addons/Scripts.pbo" "$BUILD/dabs-src" >/dev/null 2>&1 || { echo "cannot unpack Dabs"; exit 1; }
	sed -i -E 's/^(typedef map<string, RelayCommand> RelayCommandHashMap)\r?$/\1;/' "$BUILD/dabs-src/3_Game/DabsFramework/MVC/Types/Types.c"
	makepbo -B -P -@='DabsFramework\Scripts' "$BUILD/dabs-src" "$PATCHED/addons/Scripts.pbo" >"$BUILD/makepbo-dabs.log" 2>&1 || { cat "$BUILD/makepbo-dabs.log"; exit 1; }
	ln -sfn "$PATCHED" "$TREE/@DabsFramework"
else
	ln -sfn "$WORKSHOP/2545327648" "$TREE/@DabsFramework"
fi
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
		sleep 1
		waited=$((waited + 1))
		local log
		log=$(ls -t "$PROF"/script_*.log 2>/dev/null | head -1)
		if [ -n "$ready" ] && [ -n "$log" ] && grep -qE "$ready" "$log"; then found=1; sleep 1; break; fi
		[ "$waited" -ge "$RUNTIME" ] && break
	done
	local child
	child=$(pgrep -P "$pid" | head -1)
	kill "$pid" 2>/dev/null
	[ -n "$child" ] && kill "$child" 2>/dev/null
	for _ in 1 2 3 4; do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
	[ -n "$child" ] && kill -9 "$child" 2>/dev/null
	kill -9 "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
	cat "$PROF"/script_*.log >"$BUILD/last-$label.log" 2>/dev/null
	[ "$found" = 1 ] || [ -z "$ready" ]
}

# script_errors <log>: (E) lines, minus engine noise: the JSON error of the broken-config case and the
# vanilla 1.30 experimental leak at shutdown (also in a run without any mod; a leak of ours would add a line)
script_errors() {
	grep -hE 'SCRIPT.*\(E\)' "$1" | grep -vE "JSON ERROR|Leaked 'BunkerBroadcastManager'|Total Leaks \(1x\)"
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
		mkdir -p "$CFG_DIR/DZE"
		echo '{ "maps": [] }' >"$CFG_DIR/config.json"
		;;
	noconfig)
		rm -rf "$CFG_DIR"
		;;
	broken)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/DZE"
		echo '{ "maps": [ { "file": "a.dze" } ' >"$CFG_DIR/config.json"
		cp "$CFG_DIR/config.json" "$SCEN_DIR/config.json.orig"
		;;
	realdze)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/DZE"
		cp "$ROOT/tests/dze/"*.dze "$CFG_DIR/DZE/"
		echo '{ "networked": ["@doors", "@ladders", "@actions", "@fixedItems"], "debugLog": true }' >"$CFG_DIR/config.json"
		;;
	fixtures)
		rm -rf "$CFG_DIR"
		mkdir -p "$CFG_DIR/DZE"
		write_fixture_config "$2"
		if [ "$2" = reversed ]; then touch "$SCEN_DIR/reversed"; fi
		;;
	esac
}

# write_fixture_config <normal|reversed>: the maps themselves are written by the test addon (Prepare)
write_fixture_config() {
	# only fixture_b's contents need an override. "reversed": the test addon saves them as fixture_a.dze
	local maps='{ "file": "fixture_b.dze", "networked": ["!Land_Container_1Bo"] }'
	[ "$1" = reversed ] && maps='{ "file": "fixture_a.dze", "networked": ["!Land_Container_1Bo"] }'
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
	local want=DAYZ_1_29
	[ "${DZOMS_SERVER:-stable}" = exp ] && want=DAYZ_1_30
	grep -q "$want" "$BUILD/last-boot-mod.log" || { echo "engine define $want missing: wrong server?"; return 1; }
	echo "engine: $want"
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
	if script_errors "$BUILD/last-boot-mod.log" | grep .; then return 1; fi
	return 0
}

# scenario_default <log name> <scenario> [config variant]
scenario_default() {
	local name=$1 scenario=${2:-$1}
	scenario_prepare "$scenario" "$3"
	run_server "$name" "@DabsFramework;@dzo-mapstream;@dzo-mapstream-tests" '\[dzo-mapstream-test\] DONE' || { echo "$name: no DONE line"; { script_errors "$BUILD/last-$name.log"; grep -h FAIL "$BUILD/last-$name.log"; } | head -20; return 1; }
	local log=$BUILD/last-$name.log
	# the engine itself logs "(E): JSON ERROR" when the broken-config case is loaded on purpose
	if script_errors "$log" | head -20 | grep .; then return 1; fi
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

# fixtures config twice -> same digest; the same maps loaded in reverse order -> a different one
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
