# dzo-mapstream

Loads DayZ Editor map files (`.dze`) on the server and sends them to the clients as plain data
instead of thousands of networked objects. The server and every client spawn the objects locally.
Objects that only work when networked (doors, ladders, levers, ...) are spawned by the server as
networked objects. Design: `DESIGN.md`.

Target: DayZ 1.29 and 1.30.

## Installation

- Requires the Dabs Framework (Steam Workshop id 2545327648).
- Load `@dzo-mapstream` with `-mod=` on the **server and on every client**. Server and clients must
  run the same version.
- Put the map files into `<profiles>/dzo-mapstream/DZE/`. Every `*.dze` file in that folder is
  loaded, in alphabetical order (not case sensitive), nothing has to be listed. The server creates
  the folder and a default `config.json` on the first start.
- On DayZ 1.30 the server must be started with `-profiles=<dir>` (the folder is found through it).

## Files

Server profile (`-profiles=` directory):

```
dzo-mapstream/
  config.json          the configuration
  DZE/                 every *.dze file in here is loaded
```

Client profile: `dzo-mapstream/cache/<address>_<port>.bin`, one cache file per server. Delete the
file to clear the cache. A cached batch is only used when its SHA-256 matches the hash the server
announced.

## config.json

```json
{
    "maps": [
        { "file": "twistedup.dze", "networked": ["*Container*", "!Land_Container_1Bo"] },
        { "file": "old_event.dze", "disabled": true }
    ],
    "networked": ["@doors", "@ladders", "@actions", "@fixedItems", "Land_Underground_Panel*", "*WaterSpring*", "*Switch*", "*Stair*"],
    "viewDistance": 0,
    "proxyDistance": 0,
    "batchSize": 0,
    "disableCache": false,
    "debugLog": false
}
```

Every field's zero (or missing) value means "default".

| Field | Meaning | Default |
|---|---|---|
| `maps` | optional per-file settings; files in `DZE/` without an entry are loaded with the global rules | none |
| `maps[].file` | file name inside `DZE/` (no paths) | entry ignored |
| `maps[].disabled` | skip this file | loaded |
| `maps[].networked` | extra rules for this file, after the global ones | none |
| `networked` | global networked rules | no object is networked |
| `viewDistance` | clients spawn batches within this distance (m), 100..20000 | 1500 |
| `proxyDistance` | beyond this distance clients show proxies of networked objects (m), 100..20000 | 1000 |
| `batchSize` | max objects per batch, 50..1000 | 400 |
| `disableCache` | clients must not use or write a cache for this server | cache enabled |
| `debugLog` | log the networked report and per-batch details | off |

An invalid `config.json` is reported in the server log and left untouched; no maps are loaded.
An entry whose file is not in `DZE/` is reported as an error (unless it is `disabled`).

### Networked rules

The list is evaluated for each type name; the **last matching rule wins**. No match means local.

| Rule | Matches |
|---|---|
| `Land_Lighthouse` | exactly this type name |
| `*Stair*` | glob: `*` = any characters, `?` = one character |
| `!pattern` | like `pattern`, but a match makes the object local |
| `@doors` | types with doors |
| `@ladders` | objects with a ladder selection (`ladder<digits>`) |
| `@actions` | buildings that register their own actions (wells, levers) |
| `@fixedItems` | items players cannot pick up (placed targets, territory flags) |
| `!@doors`, ... | negated forms |

Matching ignores case and covers the whole type name. A pattern containing `/` or `\` is matched
against the full model path instead. Objects given as a model path (`.p3d`) use the file name without
directory and extension as their type name.

With `debugLog` the server logs every networked type with its object count and deciding rule
(`<file> networked: <Type> x<n> (<rule>)`). The number of networked and local objects per map file and
all config errors are always logged.

## Tests

Headless server tests (needs the Steam DayZ Server, the Dabs Framework in the workshop folder and
Mikero `makepbo`):

```
tools/check-style.sh
tools/run-server-tests.sh            # boot unit noconfig broken fixtures determinism
tools/run-server-tests.sh unit       # one scenario
```

### In-game test list (manual, not yet done)

The client side cannot be tested headless. Before relying on it:

1. First join downloads closest-first (order of batch requests in the client log), the area around
   the player is complete first; the cache file appears under `$profile:dzo-mapstream/cache/`.
2. Second join: the log shows all batches taken from the cache, no download.
3. Doors open, ladders can be climbed, levers and wells work, shooting range targets (workshop
   3420700027, 3034202210) react to hits and can be reset; decoration items cannot be taken.
4. Walking away and back: batches unload and load without hitches; no duplicated objects; proxies of
   networked buildings appear and disappear at `proxyDistance` without gaps or doubles (confirms the
   1000 m network range assumption).
5. Hidden world objects are hidden.
6. Once with `batchSize: 1000` and a map with long `.p3d` paths: every batch arrives.
7. `*Stair*` and `*Switch*` rules: decide whether they stay in the default config.
