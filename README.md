# dzo_mapstream

A DayZ server mod that puts big Editor-made maps (bases, events, bunkers, shooting ranges, ...) into
your server **without the usual lag and network load**.

Workshop page: https://steamcommunity.com/sharedfiles/filedetails/?id=3812840035

You build a map with the DayZ Editor, save it as a `.dze` file and drop the file into a folder on
your server. The mod does the rest.

Supported DayZ version: 1.30 (tested on the 1.30 stable server, build 25963285).

## What it does (and what "networked" means)

Normally the server keeps track of every object in the world and sends it to every player. That is
called a **networked** object. It is needed for anything players interact with: a door has to be
open or closed for everybody at the same time. But it is expensive. A map with thousands of
decorative objects (fences, crates, walls, ...) can slow the server down and make players join slowly.

This mod does it differently for most objects:

- The server sends the map to each player **once**, as a small block of plain data. The player's
  game saves it, so the next visit needs no download.
- Every player's game, and the server, then create these objects **locally**. Nobody has to keep
  them in sync, because they never change. Zombies and AI treat them as normal, solid objects.
- Objects that must work for everybody (doors, ladders, levers, wells, ...) stay **networked**: the
  server creates them the normal way. The mod decides which ones with a list of rules (see
  [Networked rules](#networked-rules)); the defaults already cover the usual cases.
- Everything else becomes networked **only near a player who can use it**: the DayZ client cannot
  climb or vault onto objects that exist only locally, because the server must agree on which object
  the player is standing on. So the server swaps the local objects around a player (150 m on foot by
  default) for real networked copies, and swaps them back once nobody is near. A player in an
  aircraft or a fast car, or sprinting, gets nothing swapped, so fast movers cost the server
  almost nothing. See [Climbing and proximity networking](#climbing-and-proximity-networking).
- Objects that you deleted in the Editor (to clear space on the map) are hidden in the game, too.

## Installation

1. Get the mod and its requirement from the Steam Workshop and put them into your server directory:
   - this mod: https://steamcommunity.com/sharedfiles/filedetails/?id=3812840035
   - Dabs Framework (required): https://steamcommunity.com/sharedfiles/filedetails/?id=2545327648

   Your hosting provider may have a mod manager that does this for you.
2. Start the server with both mods in the `-mod=` parameter, for example:

   ```
   -mod=@DabsFramework;@dzo_mapstream -profiles=profiles
   ```

   Use the names of the mod folders you have, and keep Dabs Framework first. Use `-mod=`, not
   `-servermod=`: players need the mod too, and the launcher then downloads it for them
   automatically. They do not have to do anything.
3. `-profiles=<folder>` is required (on DayZ 1.30 the mod needs it to find its folder). Everything
   the mod stores on the server lives in that folder.
4. If your server checks signatures (`verifySignatures`), copy the `.bikey` files from the `keys`
   folders of both mods into the `keys` folder of your server.
5. Start the server once. It creates the folders and the settings file described below.
6. Copy your `.dze` files into `<profiles>/dzo/mapstream/DZE/` and **restart the server**.

Whenever the mod is updated on the Workshop, update the server's copy too, otherwise players
cannot load the maps.

## Where to find things

All paths are inside the folder you gave with `-profiles=`:

```
dzo/mapstream/
  config.json     the settings (created on the first start)
  DZE/            put your .dze map files here
```

- **Every `*.dze` file in `DZE/` is loaded**, in alphabetical order. You do not have to list the
  files anywhere. To remove a map, remove its file (or disable it, see below) and restart.
- Changes to `config.json` or to the files in `DZE/` need a server restart.
- Players do not need to configure anything. Their game keeps a cache in its own profile folder
  (`dzo/mapstream/cache/`); deleting it makes the next join download everything again.

## Checking that it works

Look at the newest `script_*.log` in your profiles folder and search for `[dzo-mapstream]`. You
should see one line per map file, for example:

```
[dzo-mapstream] harbor_market.dze: 12 networked, 230 local, 0 invalid, 0 editor-only, 0 duplicates, ...
[dzo-mapstream] 1253 objects (164 networked, 1089 local) in 23 batches, digest ...
[dzo-mapstream] ready: digest ...
```

A *batch* is a group of nearby objects. Players load a batch when they come close to it (see
`viewDistance` below), so the map appears around them as they walk or fly.

If something goes wrong, the log says why (lines with `WARNING` or `ERROR`).

## config.json

The server writes a working default file on the first start. A settings file that has an error is not
touched and no maps are loaded; the error is in the log. A complete example:

```json
{
    "maps": [
        { "file": "harbor_market.dze", "networked": ["*Container*", "!Land_Container_1Bo"] },
        { "file": "bunker_entrance.dze", "disabled": true }
    ],
    "networked": ["@doors", "@ladders", "@actions", "@fixedItems", "Land_Underground_Panel*", "*WaterSpring*", "*Switch*", "*Stair*"],
    "viewDistance": 0,
    "proxyDistance": 0,
    "batchSize": 0,
    "disableCache": false,
    "debugLog": false,
    "configVersion": 1
}
```

A value of `0` (or a missing line) means "use the default".

| Setting | Meaning | Default |
|---|---|---|
| `networked` | The rules that decide which objects are networked (see below). | the list above |
| `maps` | Optional. Special settings for single files in `DZE/`. Files without an entry here are loaded normally. | empty |
| `maps[].file` | The file name, without folder. | entry ignored |
| `maps[].disabled` | `true` = do not load this file (without deleting it). | loaded |
| `maps[].networked` | More rules only for this file, applied after the global ones. | none |
| `viewDistance` | Players see (and download) a batch of objects when they are closer than this many meters. 100 to 20000. | 1500 |
| `proxyDistance` | Networked objects only exist for a player when they are fairly close (about 1000 m). Further away, the player sees a plain copy so the map does not have holes. 100 to 20000. | 1000 |
| `batchSize` | Maximum objects per batch. 50 to 1000. | 400 |
| `disableCache` | `true` = players do not keep a copy of the map; they download it on every join. | cache on |
| `networkRadius` | Radius in meters around a player on foot in which objects become networked. 5 to 500. | 150 |
| `vehicleRadius` | The same for a slow vehicle. 5 to 500. | 40 |
| `maxFootSpeed` | On foot and faster than this (m/s): no networked objects for that player. | 6 |
| `maxVehicleSpeed` | In a vehicle and faster than this (m/s): none. | 8 |
| `maxHeight` | Higher above the ground than this (m), for example in a helicopter: none. | 20 |
| `maxNetworked` | The most objects networked at one time by proximity networking. 1 to 20000. | 6000 |
| `proximityExclude` | Object types that are never made networked by proximity, for example plants: patterns like in the networked rules, without `@`. | none |
| `disableProximity` | `true` = proximity networking off: objects stay local, and players cannot climb them. | on |
| `configVersion` | Written by the mod. Do not change it. | 1 |
| `debugLog` | `true` = write extra details to the log (which objects are networked and why, where every batch is). | off |

### Updates of config.json

`config.json` has a version. When a mod update needs a newer layout, the server migrates your file on
the first start: it copies the old file to `config.json.v1.bak` (the number is the old version), then
writes the new one and logs what changed. Your rules and settings are kept. If the backup cannot be
written, the file stays as it is and the server uses the migrated settings until the next start. A file
from a newer mod version is used as it is and never changed.

### Networked rules

The `networked` list says which objects are networked; all others are local. Each object type is
checked against the list from top to bottom and **the last matching rule wins**.

| Rule | Matches |
|---|---|
| `Land_Lighthouse` | exactly this object type |
| `*Stair*` | `*` stands for any characters, `?` for exactly one |
| `!pattern` | like `pattern`, but the object becomes **local** (to switch something off again) |
| `@doors` | objects with doors |
| `@ladders` | objects with a ladder |
| `@actions` | buildings that offer their own actions (wells, levers) |
| `@fixedItems` | placed items players cannot pick up (targets, territory flags) |
| `!@doors`, ... | the same, but make the objects local |

Upper and lower case do not matter. In doubt, keep the defaults. If a door, ladder, lever or well
from your map does not work, add a rule that makes that type networked. With `debugLog` the log lists
every networked type and which rule matched it, which tells you what to change.

## Climbing and proximity networking

Players can only climb or vault onto an object that exists **networked**: the DayZ server checks the
climb and has to know which object the player grabs, and objects that every game creates locally have
no identity it could use. Networking the whole map would bring back the load this mod avoids, so the
server networks only what a player can actually use:

- A player on foot who is slower than `maxFootSpeed` (6 m/s, walking and jogging) gets a zone of
  `networkRadius` (150 m). A player in a vehicle slower than `maxVehicleSpeed` (8 m/s) gets
  `vehicleRadius` (40 m). Anybody higher than `maxHeight` above the ground, dead, or faster gets no zone.
  A zone opens after the player has stayed in these limits for two seconds.
- Objects inside a zone are replaced by networked copies, nearest first, a few per frame. Players
  see no change: the game removes its local copy only after the networked one has arrived.
- An object goes back to local when no zone has been near it for 30 seconds (and it is farther than
  1.5 times the radius, so nothing flickers at the border).
- The server keeps at most `maxNetworked` objects networked at once; the nearest ones win.
- Objects the networked rules already make networked (doors, ladders, ...) are not touched.

If the server is busy, make `networkRadius` smaller or `maxNetworked` lower, and add object types that
nobody climbs (plants, small decoration) to `proximityExclude`. With `debugLog` the server logs once a
minute how many objects are networked, how many were swapped in the last minute, the time spent, and
the object types promoted most often; the last list is what to put into `proximityExclude`.
`disableProximity` switches all of this off.

The server keeps the object it swaps out for five seconds, so the zombies' path graph (the AI's idea of
where walls are) has time to take the new object in.

Server and players must run the same version of the mod (the protocol changed). The first join
after an update downloads the maps again, because model objects are now stored at the position the
engine puts them, which is where the Editor shows them.

## Troubleshooting

- **Nothing appears in game.** Players only see objects within `viewDistance` of them. Stand at the
  place where you built the map. The log lines `batch N: ... center <x, y, z>` (with `debugLog`) show
  where the batches are.
- **A map file adds no objects.** If the log line for the file counts all objects as `editor-only`,
  the file marks them as Editor-only helper objects, which are skipped on purpose. Change that in
  the Editor and save the file again.
- **Players cannot join, "mod not on the server" or a signature error.** Check that the mod is in
  `-mod=` (not `-servermod=`), that the server's copy is up to date and that the `.bikey` files are
  in the server's `keys` folder.
- **A door or ladder does not work.** Make that object type networked (see the rules above).

## License

Copyright (c) 2026 Bernd Zeimetz <bernd@bzed.de>

Licensed under the Bohemia Interactive DayZ Public Licence (DPL): noncommercial, DayZ only,
attribution required. See `LICENSE` (source: https://www.bohemia.net/en/licenses/dayz-public-license-dpl).

Developers: see `DEVELOPMENT.md` and `DESIGN.md`.

## Disclaimer

This mod is not affiliated or authorized by Bohemia Interactive a.s. Bohemia Interactive, ARMA, DAYZ
and all associated logos and designs are trademarks or registered trademarks of Bohemia Interactive a.s.
