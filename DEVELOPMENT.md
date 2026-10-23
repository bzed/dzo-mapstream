# Development

Notes for working on the mod. The server owner documentation is in `README.md`, the design in
`DESIGN.md`.

Repository layout:

```
dzo_mapstream/   the addon (config.cpp, scripts/3_Game, 4_World, 5_Mission)
tests/           test addon (headless server tests) and sample .dze files
tools/           build, style check and test runner
pack/            packaging project for the Workshop upload
```

## Tests

Headless server tests (needs the Steam DayZ Server, the Dabs Framework in the workshop folder and
Mikero `makepbo`):

```
tools/check-style.sh
tools/run-server-tests.sh            # boot unit noconfig broken fixtures determinism
tools/run-server-tests.sh unit       # one scenario
```

### Real client check (headless)

`tools/run-client-view.sh <out-dir>` starts a server with `tests/dze`, joins a headless 1.30 client (needs Steam, sway,
grim; one game at a time), walks the player to 35 m in front of each object and saves a screenshot per stop
(`stopN.png`). The client log in the Proton prefix (`.../AppData/Local/DayZ/script_*.log`) has the `[dzo-mapstream]`
status and swap counters.

### In-game test list (manual)

The client side cannot be tested headless. Confirmed in a real game (1.29 client, signed Workshop
build): objects appear within `viewDistance`, first join downloads and writes the cache, a second
join takes every batch from the cache, and zombies path around the spawned buildings. Still open
are the points below that are not listed here as confirmed. Before relying on the rest:

1. First join downloads closest-first (order of batch requests in the client log), the area around
   the player is complete first; the cache file appears under `$profile:dzo/mapstream/cache/`.
2. Second join: the log shows all batches taken from the cache, no download.
3. Doors open, ladders can be climbed, levers and wells work, shooting range targets (workshop
   3420700027, 3034202210) react to hits and can be reset; decoration items cannot be taken.
4. Walking away and back: batches unload and load without hitches; no duplicated objects; proxies of
   networked buildings appear and disappear at `proxyDistance` without gaps or doubles (confirms the
   1000 m network range assumption).
5. Hidden world objects are hidden.
6. Once with `batchSize: 1000` and a map with long `.p3d` paths: every batch arrives.
7. `*Stair*` and `*Switch*` rules: decide whether they stay in the default config.
8. Proximity networking (DESIGN 13.8): climb and vault onto objects that are local a moment before, also
   onto a higher part of the same object; no hole and no double at the swap in both directions; scaled
   objects (piers, sidewalks) have the right size close up, far away and after a log out and in; model
   objects (`.p3d`) stand where the Editor shows them (compare a few positions); a car driven past at
   50 km/h, a helicopter at 40 m and a sprint swap nothing (`debugLog` shows the counters); getting out of
   a car networks the area after two seconds; login time compared with the previous release; zombies path
   around walls, networked and local.

## Packaging and Workshop upload

`pack/` is a project for the dayz-dev skill's `dayz-mod-pack.sh` (armake2, signing). `pack/src/dzo_mapstream`
links to the addon folder; `pack/secrets/` holds the private key and is git-ignored (back it up).

```sh
P=~/.claude/skills/dayz-dev/scripts/dayz-mod-pack.sh
$P -C pack build && $P -C pack check     # pack/build/@dzo_mapstream, the folder to upload
DZOMS_PACKAGE=1 tools/run-server-tests.sh
$P -C pack publish-hint                  # prints the `workshop create/update` commands
```

Server owners copy `pack/keys/dzo_mapstream.bikey` into their `keys/` folder.
The tests run on the 1.30 stable server by default; `DZOMS_SERVER=exp` runs them on the experimental server. For
the experimental server the runner patches a private copy of the Dabs Framework (a missing `;` in `Types.c`, the
stable server accepted the workshop build as is).

Signing note: DayZ's own `DSSignFile` includes `.c` script files in the third signature hash. The
armake2 used for `pack/` must do the same (the bzed fork with the `.c` fix in `src/sign.rs`), otherwise
a server rejects the signature of this script-only mod. A signature made by armake2 can be compared
byte for byte with `DSSignFile.exe` from DayZ Tools (run under Proton).
