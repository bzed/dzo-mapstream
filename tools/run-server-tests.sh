#!/bin/bash
# Headless server tests. Usage: tools/run-server-tests.sh [scenario...]   (default: unit)
# Env: DZOMS_TREE (server tree, default ~/dayz-testserver-dzoms), DZOMS_RUNTIME (seconds, default 120)
SKILL=/home/bzed/.claude/skills/dayz-dev/scripts
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TREE=${DZOMS_TREE:-$HOME/dayz-testserver-dzoms}
RUNTIME=${DZOMS_RUNTIME:-120}
BUILD=$ROOT/build
PROF=$TREE/profiles
SCEN_DIR=$PROF/dzo-mapstream-tests
CFG_DIR=$PROF/dzo-mapstream
[ $# -eq 0 ] && set -- unit

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
	esac
}

scenario_boot() {
	local base mod
	run_server boot-vanilla "@DabsFramework" "" || { echo "boot-vanilla: server did not run"; return 1; }
	run_server boot-mod "@DabsFramework;@dzo-mapstream" '\[dzo-mapstream\] ready' || { echo "boot: no ready line"; return 1; }
	base=$(module_count "$BUILD/last-boot-vanilla.log" Game)
	mod=$(module_count "$BUILD/last-boot-mod.log" Game)
	echo "Game module files: vanilla+Dabs $base, with mod $mod"
	if [ -z "$base" ] || [ -z "$mod" ] || [ "$mod" -le "$base" ]; then echo "mod scripts not loaded (Game)"; return 1; fi
	base=$(module_count "$BUILD/last-boot-vanilla.log" Mission)
	mod=$(module_count "$BUILD/last-boot-mod.log" Mission)
	echo "Mission module files: vanilla+Dabs $base, with mod $mod"
	if [ -z "$base" ] || [ -z "$mod" ] || [ "$mod" -le "$base" ]; then echo "mod scripts not loaded (Mission)"; return 1; fi
	if grep -hE 'SCRIPT.*\(E\)' "$BUILD/last-boot-mod.log"; then return 1; fi
	return 0
}

scenario_default() {
	local name=$1
	scenario_prepare "$name"
	run_server "$name" "@DabsFramework;@dzo-mapstream;@dzo-mapstream-tests" '\[dzo-mapstream-test\] DONE' || { echo "$name: no DONE line"; grep -hE 'SCRIPT.*\(E\)|FAIL' "$BUILD/last-$name.log" | head -20; return 1; }
	local log=$BUILD/last-$name.log
	if grep -hE 'SCRIPT.*\(E\)' "$log" | head -20 | grep .; then return 1; fi
	grep -h 'dzo-mapstream-test\] FAIL' "$log"
	grep -q '\[dzo-mapstream-test\] DONE pass=[0-9]* fail=0$' "$log"
}

status=0
for s in "$@"; do
	case "$s" in
	boot) scenario_boot ;;
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
