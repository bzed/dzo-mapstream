# dzo_mapstream: design

Loads DayZ Editor map files (`.dze`) on the server and transfers them to the clients
as plain data instead of thousands of networked objects. The server and every
client spawn the objects locally. Objects that only work when networked (doors,
ladders, levers, ...) are spawned as networked objects by the server.

Target: DayZ 1.29 and 1.30. Status: implemented; section 13 (proximity networking) was added after the first
in-game tests and is described there, with the findings it rests on.

## 1. Goals

1. The map around the player exists as early as possible after joining, on the
   client and on the server, so client and server collision always agree.
2. Map data is downloaded closest-first and spawned closest-first.
3. Clients cache the map data. A cached batch is only used when its SHA-256 matches
   the hash the server announced, so an edited cache is never spawned.
4. Admins list the map files and the networked object rules in one JSON file.
   No directory scanning: `FindFile` is broken in DayZ 1.30.
5. Objects that need networking are chosen by readable rules (section 5).
6. No unbounded work per frame, no unbounded memory, no trust in client input.

Not in scope for the first version: other map formats, a download progress UI,
cache encryption (planned, section 12).

## 2. Dependencies and packaging

- Dabs Framework, patch `DF_Scripts`. Provides `EditorSaveData`,
  `EditorObjectData`, `EditorDeletedObjectData` and
  `GetDayZGame().GetSuppressedObjectManager()`.
- `config.cpp` (text, not binarized), `CfgPatches` class `dzo_mapstream`,
  `requiredAddons[] = {"DZ_Data", "DF_Scripts"}`, script modules
  `3_Game`, `4_World` and `5_Mission` under `dzo_mapstream/scripts/`.
  Almost all logic lives in 3_Game: the RPC entry point (`DayZGame.OnRPC`) is in
  3_Game and cannot reach classes of later modules, and nearly everything needed
  (`Object`, `Building`, `CGame`, the Dabs Framework classes) is available there.
  The exceptions are `@actions` and `@fixedItems`, which need `BuildingBase` and
  `ItemBase` (4_World): 3_Game declares `DZOMS_ObjectProbe` with overridable
  `bool HasOwnActions(Object obj)` and `bool IsFixedItem(Object obj)` (both return
  false), 4_World provides a subclass that implements them, and the mission hook
  hands an instance of it to the server world.
  5_Mission otherwise only holds the mission hooks.
- The PBO is loaded with `-mod=` on server and clients (both run the same scripts).
- All script classes use the prefix `DZOMS_` (DZO MapStream), all modded-class members the prefix
  `m_DZOMS_`, all log lines start with `[dzo-mapstream]`.
- Source files use Unix line endings, no trailing whitespace.

## 3. Terms

- **Map file**: one `*.dze` file in the `DZE/` folder.
- **Object**: one entry of `EditorSaveData.EditorObjects` that gets spawned.
- **Type name**: the object's class name. For objects given as a model path
  (`Type` contains `.p3d`), the file name without directory and `.p3d`,
  lower case, e.g. `dz\...\ancient_ruins_staircellar.p3d` -> `ancient_ruins_staircellar`.
- **Networked object**: spawned by the server without `ECE_LOCAL`; clients get it
  through the normal network bubble and never spawn it themselves (except as a
  distant proxy, section 9.6).
- **Local object**: spawned by the server and by every client with `ECE_LOCAL`.
- **Promoted object**: a local object that the server has swapped for a networked copy because a player
  is near it (section 13); it goes back to local when nobody needs it.
- **Batch**: a group of up to `batchSize` objects close to each other. Unit of
  download, caching, hashing, spawning and unloading.
- **Manifest**: per-server list of batches (bounds + hash) plus client settings.

## 4. Files

Server profile (`$profile:` = the `-profiles=` directory):

```
dzo/mapstream/
  config.json          the configuration (section 5)
  DZE/                 every *.dze file in here is loaded (section 6.1)
```

Client profile:

```
dzo/mapstream/
  cache/<address>_<port>.bin    one cache file per server (section 10)
```

`<address>` comes from `g_Game.GetHostAddress(out address, out port)`; `:` is
replaced by `-` (IPv6 addresses are not valid Windows file names).

## 5. Configuration

### 5.1 Example

```json
{
    "maps": [
        { "file": "twistedup.dze", "networked": ["*Container*", "!Land_Container_1Bo"] },
        { "file": "old_event.dze", "disabled": true }
    ],
    "networked": [
        "@doors",
        "@ladders",
        "@actions",
        "@fixedItems",
        "Land_Underground_Panel*",
        "*WaterSpring*",
        "*Switch*",
        "*Stair*"
    ],
    "viewDistance": 0,
    "proxyDistance": 0,
    "batchSize": 0,
    "disableCache": false,
    "debugLog": false,
    "configVersion": 1
}
```

### 5.2 Fields

| Field | Type | Meaning | `0` / missing means |
|---|---|---|---|
| `maps` | array | optional per-file settings; the files themselves come from `DZE/` | no settings |
| `maps[].file` | string | file name inside `DZE/` | entry ignored (warning) |
| `maps[].disabled` | bool | skip this file | file is loaded |
| `maps[].networked` | array of rules | extra rules for this file, appended after the global rules | none |
| `networked` | array of rules | global networked rules (5.3) | no object is networked |
| `viewDistance` | float, m | client spawns batches within this distance | 1500 |
| `proxyDistance` | float, m | distance beyond which clients show proxies of networked objects | 1000 |
| `batchSize` | int | max objects per batch (clamped to 50..1000) | 400 |
| `disableCache` | bool | clients must not use or write a cache for this server | cache enabled |
| `debugLog` | bool | log the networked report (5.5) and per-batch details | off |
| `configVersion` | int | layout version of this file; written by the mod, admins do not edit it | version 1 (a file from before versioning) |
| `networkRadius`, `vehicleRadius` | float, m | proximity networking: zone radius of a player on foot / in a slow vehicle (clamped 5..500) | 150 / 40 |
| `maxFootSpeed`, `maxVehicleSpeed`, `maxHeight` | float | faster (m/s) or higher above ground (m): no zone | 6 / 8 / 20 |
| `maxNetworked` | int | most objects networked by proximity at once (1..20000) | 6000 |
| `proximityExclude` | array of name patterns | types never networked by proximity (no `@` tokens) | none |
| `disableProximity` | bool | switch proximity networking off | on |

**Every field is designed so that its zero value is the default.** Verified on 1.29:
`JsonFileLoader<T>.LoadFile()` does not keep the class's initial values for fields
missing from the file; they come back as `0`, `false`, empty string or an empty or
`NULL` array. A field like `"enabled": true` would therefore silently turn into
`false` when an admin leaves it out. After loading, the config is normalized:
`0` numbers get their defaults, `NULL` arrays become empty arrays, values out of
range are clamped with a warning.

### 5.3 Networked rules

A rule list is evaluated top to bottom for each type name; **the last matching rule
wins** (like `.gitignore`). No match means local.

| Rule | Matches |
|---|---|
| `Land_Lighthouse` | exactly this type name |
| `*Stair*` | glob: `*` = any characters (also none), `?` = exactly one character |
| `!pattern` | same as `pattern`, but a match makes the object local |
| `@doors` | objects whose type has doors (`Building.GetDoorCount() > 0`) |
| `@ladders` | objects with a ladder (a model selection named `ladder<digits>`) |
| `@actions` | buildings that register their own actions (`BuildingBase` with at least one `AddAction()` in `SetActions()`), e.g. wells, underground panel levers |
| `@fixedItems` | items that cannot be taken into hands (`ItemBase.CanPutIntoHands(NULL) == false`), e.g. placed shooting targets, territory flags |
| `!@doors`, `!@ladders`, `!@actions`, `!@fixedItems` | negated forms |

- Matching is case-insensitive and always against the whole type name.
- A pattern containing `/` or `\` matches against the full model path instead
  (backslashes normalized to `/`, lower case), e.g. `"dz/structures_bliss/*"`.
- Unknown `@` tokens and empty patterns are reported as config errors and ignored.
- Effective rules for an object = global `networked` followed by the `networked`
  list of its map file.

Why these rules: actions are server-authoritative, and a client can only target
objects the server knows, i.e. networked ones. On a local object the player sees
the action fail or nothing happens.

- `@doors` / `@ladders`: opening doors and climbing ladders are player actions on
  the building. Checked against the DayZ 1.29 data of two test maps (289 types):
  `GetDoorCount() > 0` gives the same result as the class config
  `CfgVehicles <type> Doors`, and doors + `ladder<n>` selections select exactly the
  24 types that have door or ladder memory points. `Building.GetLaddersCount()`
  is useless for this: it returns 0 for every tower, also for networked ones.
- `@actions`: buildings whose script class adds its own actions. In the test maps
  and vanilla probes this selects exactly `Land_Misc_Well_Pump_Blue`,
  `Land_Misc_Well_Pump_Yellow` (drink, wash hands) and
  `Land_Underground_Panel_Lever`, and nothing else. Counted with a
  `modded class BuildingBase` that overrides `AddAction()` and calls
  `InitializeActions()` on the probe object; `BuildingBase` is 4_World (6.2).
- `@fixedItems`: interactive objects of content mods are often items, not
  buildings. Checked with Shooting Range Targets (workshop 3420700027) and
  Shooting Range Objects (3034202210): their targets are `ItemBase` classes that
  register hits on the server (`EEHitBy`), sync their state
  (`RegisterNetSyncVariableBool`, animation phases) and partly bring their own
  actions (`ActionResetTarget`), so they only work networked. "Has own actions"
  cannot tell them apart from decoration (binoculars, cans and lighters have
  actions too) and `IsTakeable()` is true for all of them. What they share:
  players cannot pick them up. `CanPutIntoHands(NULL) == false` selects exactly
  the 56 placed target classes of both mods plus vanilla `TerritoryFlag`, and
  none of: the target kits, binoculars, apple, opened cans, lighter, rag, AKM,
  barrel, fireplace, power generator, sea chest, car tent. All target classes
  spawn networked with a valid network id. Only `CanPutIntoHands()` is called
  with `NULL`: `CanPutInCargo(NULL)` is not null-safe (NULL pointer exception in
  `BarrelHoles_Red`). Evaluated once per type on the probe object.
- Items that do not match stay local: decoration on the map, visible and
  solid, but not takeable (the client cannot act on local objects). An admin who
  networks takeable items by name makes them lootable.
- Name rules for the rest: the Sakhal water spring and the plain underground
  panel have no actions of their own (their logic sits in player actions and
  network synced state), so no automatic rule finds them; the default config
  lists them by name. Stairs and switches are listed by name as a precaution:
  players are said to stutter on stairs that only exist locally, and switches
  of content mods may carry their logic like the panel does. Neither is
  verified yet; the in-game test (section 11) decides whether they stay.
- Not detectable: proxies inside a model. `IEntity.GetChildren()` returns no
  children for any of the 289 types and the script API has no way to list
  model proxies.
- Deliberately no rule for "has its own script class": that also matches
  houses, street lamps, burning tire piles and decoration items (binoculars,
  opened cans), and networked items can be picked up by players.

### 5.4 Glob matching

Iterative two-pointer match with one backtrack point, on lower-cased strings
(`p` = pattern, `s` = name, `star` = position of the last `*`, `mark` = position
in `s` matched against it):

1. If `p[i]` is `?`, or equal to `s[j]`: advance both.
2. Else if `p[i]` is `*`: `star = i`, `mark = j`, advance `i`.
3. Else if a `*` was seen: `i = star + 1`, `mark++`, `j = mark`.
4. Else: no match.
5. When `s` is consumed: skip trailing `*` in `p`; match if `p` is consumed too.

Linear for patterns with one `*`, worst case O(len(p) * len(s)), no recursion.
Results are cached per (rule list, type name), so every distinct type is matched
once per map file.

### 5.5 Report

With `debugLog` enabled the server logs, per map file, every networked type with
the number of objects and the rule that decided it:

```
[dzo-mapstream] twistedup.dze networked: Land_Misc_Well_Pump_Blue x9 (*Well_Pump*)
[dzo-mapstream] twistedup.dze networked: Land_House_1W10 x1 (@doors)
[dzo-mapstream] twistedup.dze local by rule: Land_Container_1Bo x3 (!Land_Container_1Bo)
```

Always logged: number of networked and local objects per map file, and
config errors.

### 5.6 Missing or broken config

- File missing: write the default config (example above with an empty `maps`
  list; the `DZE/` folder is created, too), then **load it like any other config**, so defaults are effective on the
  very first start.
- File present but invalid JSON: log the error from `LoadFile()`, leave the file
  untouched, load no maps. Never overwrite an admin's file.
- File written with `configVersion` = `DZOMS_Config.CURRENT_VERSION` (1 so far). The default
  file therefore never triggers a migration. Version 1 is the layout without the field, which
  is why `0` / missing means 1.
- Version check after `LoadFile()` succeeded, before `Normalize()`:
  - same version: nothing to do;
  - newer version (file of a newer mod): log an error, use the file as it is (unknown fields
    are ignored by `JsonFileLoader`), never write it;
  - older version: apply the steps `v -> v+1` in order (`DZOMS_ConfigMigration.Step`), each one on
    the loaded, not yet normalized config. A missing step is logged as an error; the config is
    used at the version reached and the file is not written. After all steps: back up the
    admin's file with `CopyFile(path, path + ".v<v>.bak")` (an existing backup of that name is
    deleted first). If the backup fails, log an error and **do not write**; the migrated config
    is used in memory and the next start tries again. Then write the migrated file with
    `JsonFileLoader.SaveFile` (a failure is logged, the migrated config is still used) and log
    one line per migration. The file is written **before** normalizing, so fields that were `0`
    (= default) stay `0` in the file and keep following future default changes.
  This is the one exception to "never overwrite an admin's file", and only for a valid file of
  an older version. Rewriting goes through the config class: the file loses what the class does
  not know (misspelled keys) and its formatting; the backup keeps the original.
- Rules for later versions: a new option whose zero value is its default needs no migration
  (missing = default). A change of meaning or of a default that old files must not silently get
  needs a step. Every step gets a test with a file of the old version. `CopyFile` with
  `$profile:` paths is covered by the unit tests; the target is deleted first so the mod does not
  depend on how `CopyFile` treats an existing file.

## 6. Server

All server work happens in `MissionServer.OnInit()`, after `super.OnInit()`.

### 6.1 Loading

1. Load and normalize the config (5.6).
2. List `DZE/` with `FindFile` (pattern `*`, directories skipped, extension `.dze` in any
   case). The file system order is not defined, so the names are sorted case-insensitively;
   that order is the load order and therefore part of the batch hashes. `FindFile` ignores
   `$profile:` on 1.30 (DZEXP-134): the pattern goes through `DZOMS_FindFilePath` (copy of the
   dayz-dev skill's helper), which needs `-profiles=` on 1.30. Open files with the `$profile:`
   path, never with the resolved one.
   `maps[]` entries are per-file overrides: reject `file` values containing `..`, `/`, `\` or `:`,
   skip duplicates (case-insensitive) with a warning. A listed file that is `disabled` is
   skipped; an entry without a file in `DZE/` is an error. Per found file: `FileExist`, then
   `EditorSaveData.LoadFromFile()`. A `NULL` result is logged and the file skipped.
3. Objects: skip entries that are `NULL`, have an empty `Type`, or have
   the `EditorObjectData.EditorOnly` property set. The `EDITOR_ONLY` bit in `Flags`
   is ignored: real files exist whose objects all carry it (flags 158) and are meant to be
   seen (`[NonSerialized()]` does not matter for the binary reader, which reads the property).
4. Skip exact duplicates: same type and same position rounded to 1 cm, using a
   `map<string, bool>` keyed by `type|x|y|z`. O(n), no pairwise comparison.
5. Hidden world objects (`EditorHiddenObjects`): `FindObject()` (Dabs Framework,
   radius 0.1 m), skip `NULL` and duplicates, collect all, then call
   `GetSuppressedObjectManager().SuppressMany(list)` once. Dabs Framework sends the
   list to every client when it connects (`InvokeOnConnect`), so nothing else is
   needed for hidden objects.

### 6.2 Spawning on the server

Every object is spawned on the server, local or networked:

- Class objects: `CreateObjectEx(type, pos, flags)`, then `SetOrientation(ori)`,
  `SetScale(scale)` if `scale != 1`, `Update()`.
  - local: `ECE_LOCAL | ECE_SETUP | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR`
  - networked: the same without `ECE_LOCAL`. `ECE_NOPERSISTENCY_WORLD` is
    required, otherwise the objects end up in the persistence and get duplicated
    on the next restart.
- Model objects (`.p3d`): `CreateStaticObjectUsingP3D(path, pos, ori, scale, createLocal)` with the file's
  position (the engine puts the model's base there) and orientation (radians), the scale as a creation
  parameter. The server then rewrites the entry to the object's actual origin and yaw in degrees and marks
  it `FLAG_ORIGIN`; clients and respawns place exactly there (13.10d). Class objects get `SetPosition`,
  `SetOrientation` (degrees) and `SetScale` after creation.
- After all maps are loaded, `ProcessMarkedObjectsForPathgraphUpdate()` is called once: `ECE_UPDATEPATHGRAPH`
  only marks the objects, and zombies ignore them until the marks are processed (13.10).
- A failed spawn (`NULL`) is logged once per type with the count, the object stays
  in the data (clients may have the content mod even if the server does not).

Deciding networked vs local per object, without spawning anything twice where
avoidable:

1. Evaluate the rule list for the type name. If no rule needs the object
   (`@doors`, `@ladders`, `@actions`, `@fixedItems`) to decide, the result is final: spawn once,
   directly in the right mode.
2. Otherwise spawn local, evaluate those rules on that object, and if the final
   result is "networked", delete it and spawn networked.
3. Cache the result per (type name, map file rule list). Further objects of that
   type spawn directly in the right mode.

### 6.3 Batches

Built once after all maps are loaded, deterministic for the same config and files
(the cache depends on it):

1. Objects are in config order, then file order.
2. Grid cells of 25 m in all three axes. Cell key `floor(x/25) floor(y/25) floor(z/25)`.
   Cells are kept **in order of first appearance** (an array plus a key -> index
   map); never iterate a `map` for ordering.
3. For each unused cell in that order (seed): start a batch with it (a cell larger
   than `batchSize` is split over several batches). Collect unused cells whose
   center is within 100 m of the seed's center, add them nearest first while they
   fit. A cell that does not fit stays unused and becomes a seed later.
4. Per batch: center = mean of its object positions, radius = max distance of an
   object from the center.
5. Per batch: SHA-256 over its words (section 10.1).

Measured with two maps (1229 objects): 23 batches, loading + spawning + batching +
hashing about 210 ms.

### 6.4 Answering requests

- RPC handlers in `modded class DayZGame { override void OnRPC(...) }`, server branch.
- Ignore requests with `sender == NULL`.
- Manifest request: at most one answer per identity per second.
- Batch request: index must be `0 <= index < batchCount`, otherwise ignored. At most
  20 answers per identity per second (token bucket, refill 20/s, capacity 20).
  Excess requests are dropped silently; the client retries after its timeout.
- Rate limit state is a `map<string, ...>` keyed by `identity.GetId()` and removed
  in `MissionServer.InvokeOnDisconnect()`, so it cannot grow without bound.

## 7. Protocol

RPC ids: four consecutive ints starting at `0x445A4D01` (written as constants in
`DZOMS_Rpc`). All RPCs use `ScriptRPC` with target `NULL`, guaranteed delivery.

| Id | Direction | Payload |
|---|---|---|
| `REQUEST_MANIFEST` | client -> server | `int protocolVersion` |
| `MANIFEST` | server -> client | `DZOMS_Manifest` |
| `REQUEST_BATCH` | client -> server | `int batchIndex` |
| `BATCH` | server -> client | `DZOMS_Batch` |
| `OBJECTS_NETWORKED` | server -> all clients | `DZOMS_IdList` (batch, index, network id of the copy), section 13.5 |
| `OBJECTS_LOCAL` | server -> all clients | `DZOMS_IdList` (batch, index) |

Payload classes contain only ints, floats, bools, strings, vectors and arrays of
those. **No maps and no nested class instances**: `FileSerializer` does not restore
class instances stored inside maps (verified on 1.29: they come back as `NULL`),
while flat arrays round-trip bit-exactly through `FileSerializer` and `ScriptRPC`.

```c
class DZOMS_Manifest
{
    int protocolVersion;            // 2 (the two object messages of section 13.5)
    float viewDistance;
    float proxyDistance;
    bool disableCache;
    ref array<float> bounds;        // x, y, z, radius per batch
    ref array<string> hashes;       // lower-case hex SHA-256 per batch
}

class DZOMS_Batch
{
    int index;                      // position in the manifest
    ref array<string> types;        // distinct Type strings of this batch (class name or .p3d path)
    ref array<int> typeIndex;       // per object: index into types
    ref array<int> flags;           // per object: bit 0 = networked
    ref array<vector> positions;
    ref array<vector> orientations;
    ref array<float> scales;
}
```

A client with a different `protocolVersion` gets no manifest and logs why.
Server and clients must run the same mod version.

## 8. Client: download

Owned by `DZOMS_ClientWorld`, created in `MissionGameplay.OnInit()`, updated from
`MissionGameplay.OnUpdate()`, destroyed in `MissionGameplay.OnMissionFinish()`
(deletes every object it spawned). On the dedicated server none of this exists.

1. **Cache load** (constructor): read the cache file (10.2), hash every batch, keep
   them in `map<string hash, ref DZOMS_Batch>`. Takes about 25 ms for 23 batches.
   If `GetHostAddress()` fails this early (not verified on a client yet), load the
   cache when the manifest arrives instead, before using it.
2. **Manifest request**: on the first `OnUpdate` with
   `g_Game.GetGameState() == DayZGameState.IN_GAME`, then every 3 s until a
   manifest arrived.
3. **Manifest received** (only the first one is used):
   - store settings and bounds; validate `bounds.Count() == 4 * hashes.Count()`,
     otherwise ignore the manifest and keep requesting;
   - unless `disableCache`: for every batch whose hash is in the cache, use the
     cached batch (set its `index`), closest first;
   - drop the cache map;
   - start downloading.
4. **Download loop**, at most one request in flight:
   - pick the not yet received batch with the smallest
     `max(0, distance(camera, center) - radius)`, request it;
   - on receipt: ignore if the index is out of range or already received; check
     that the arrays have equal lengths and every `typeIndex` is in range;
     **hash it and compare with the manifest**; on mismatch drop it and request it
     again; otherwise store it and request the next one;
   - timeout 5 s: request the same batch again; after 5 consecutive timeouts wait
     30 s, then continue. Never give up for good.
5. **All received**: unless `disableCache` or nothing was downloaded, write the cache
   file with all batches in manifest order. If `disableCache` is set and a cache
   file exists, delete it.

The camera position (`g_Game.GetCurrentCameraPosition()`) is used everywhere, so
dead players and spectators work too.

## 9. Client: spawning

### 9.1 Batch states

`NOT_RECEIVED`, `UNLOADED`, `LOADING`, `LOADED`, `UNLOADING`. Every received
batch starts `UNLOADED`. Each batch keeps an array of the objects it spawned (same
order as its data, `NULL` for networked objects and failed spawns) and a cursor.

### 9.2 Distances

Every 250 ms, for every received batch: `d = max(0, distance(camera, center) - radius)`.

- `UNLOADED` or `UNLOADING`, and `d <= viewDistance` -> `LOADING`
- `LOADED` or `LOADING`, and `d > viewDistance + 200` -> `UNLOADING`
- otherwise unchanged (200 m hysteresis, no flapping at the border)

A switch from `UNLOADING` back to `LOADING` keeps the objects already spawned:
the cursor continues where it is and skips entries that still exist.

### 9.3 Per-frame work

- Spawn up to 100 objects per frame, from the `LOADING` batch with the smallest `d`
  (linear scan over batches; there are tens to a few hundred).
  A batch becomes `LOADED` when its cursor reaches the end.
- Delete up to 200 objects per frame, from the `UNLOADING` batch with the largest
  `d`. A batch becomes `UNLOADED` when all its objects are gone.
- Batches received or taken from the cache are spawned by the same mechanism,
  which makes the spawn order closest-first automatically.

### 9.4 Local spawn on the client

Only objects without the networked flag:

- class objects: `CreateObjectEx(type, pos, ECE_LOCAL | ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME)`,
  `SetOrientation`, `SetScale` if needed, `Update()`;
- model objects: `CreateStaticObjectUsingP3D(path, pos, ori, scale, true)`;
- `NULL` result (content mod missing): logged once per type, never retried.

### 9.5 Networked objects

Clients never spawn networked objects inside the network bubble; the server's
instance arrives through the network.

### 9.6 Distant proxies

Networked objects are only streamed within the server's network range, so tall
networked buildings (lighthouse, towers) would pop in late. Clients show a local
proxy beyond `proxyDistance`:

- checked every 1 s, at most 50 networked objects per frame, only for objects in
  `LOADED` batches;
- spawn a proxy when the object is farther than `proxyDistance + 50` and no
  networked instance is present;
- delete the proxy when it is closer than `proxyDistance - 50` or a networked
  instance is present;
- "networked instance present": `GetObjectsAtPosition3D(pos, 0.5)` contains an
  object that is not the proxy, has the same type name, a non-zero network id
  (`GetNetworkID(low, high)`) and is within 5 cm of `pos`.

`proxyDistance` must be at least the distance up to which the server streams these
objects, otherwise there is a gap without either. 1000 m is DayZ's default far
network range; the in-game test (section 11) has to confirm it.

## 10. Hashing and cache format

### 10.1 Batch hash

SHA-256 over a list of 32-bit words, implemented in Enforce Script (`DZOMS_Sha256`,
`HashWords(array<int>)` -> lower-case hex). Why our own: Enforce has no
cryptographic hash. `string.Hash()` is a 32-bit polynomial hash (`h*37 + byte`)
that anyone can make match, salted or not. Community Framework's SHA-256 is correct
but needed about 1 s for 126 KB of text. Hashing words with native int operations
takes about 20 ms for 1229 objects. Reading strings character by character is the
slow part in Enforce, so the hash input is numeric:

```
typeCount
per type:   length, then the characters packed 4 per word (big endian, zero padded)
objectCount
per object: typeIndex, flags,
            round(pos.x * 1000), round(pos.y * 1000), round(pos.z * 1000),
            round(ori.x * 1000), round(ori.y * 1000), round(ori.z * 1000),
            round(scale * 10000)
```

Rounding is needed because Enforce cannot read a float's bits; client and server
hash the same transmitted floats, so they always get the same words. The batch
`index` is not hashed: cached batches are matched by content, so a map update only
downloads the batches that changed.

Implementation notes for `DZOMS_Sha256` (Enforce ints are 32-bit signed):
`+` wraps like unsigned; `>>` is arithmetic, so every right shift is masked
(`(x >> n) & ((1 << (32 - n)) - 1)`); test it against the standard test vectors
through a byte front end used only in tests, and against Python's `hashlib` for
word input, including 13, 14 and 16 words (padding edge cases).

### 10.2 Cache file

Written with `FileSerializer`:

```
int  cacheVersion          // 1
int  batchCount
DZOMS_Batch per batch       // the flat class from section 7
```

Reading: version mismatch, short file or any failed `Read()` -> the whole file is
discarded (no partial use). Every batch is still hash-checked against the
manifest, so bit flips only cause a re-download of the affected batch. Measured:
about 100 KB for 1229 objects, write about 3 ms, read about 1 ms.

## 11. Tests

Server, headless, before every release:

1. Boots without script errors; script module file counts increase by the mod's
   files.
2. Config: missing file -> default file written and loaded in the same start;
   broken JSON -> error, file untouched, no maps; missing fields -> defaults.
3. Two consecutive starts produce identical batch hashes; a different map order
   in the config changes them.
4. Networked rules: every object of a networked type has a non-zero network id that
   `GetObjectByNetworkId()` resolves back to it; no local object has one;
   `!` overrides, per-map rules, `@doors`, `@ladders` each covered.
5. Cache: round trip matches all hashes; a moved (1 cm), renamed, re-flagged or
   removed object changes the hash; truncated, wrong version, random and missing
   files are rejected without script errors; one flipped byte rejects one batch.
6. Hidden objects are suppressed on the server.
7. Client code compiles: build a test PBO with `#ifdef SERVER` / `#ifndef SERVER`
   swapped and boot it on the server.

Proximity networking, headless (section 13.8): zone rules table, activation delay, cap, exclusion list,
promotion and demotion order and budgets, create-before-delete, retry delays, protocol messages, client
swap (scale, model twins, login, decoys), model placement. The real player states, the swap on a real
client, climbing and the path graph need the in-game list of 13.8.

Client, in game (cannot be automated):

1. First join downloads closest-first, the area around the player is complete
   first; the cache file is written.
2. Second join: the log shows all batches taken from the cache, no download.
3. Doors open, ladders can be climbed, levers and wells work, shooting range
   targets react to hits and can be reset; decoration items cannot be taken.
4. Walking away and back: batches unload and load without hitches; no duplicated
   objects; proxies of networked buildings appear and disappear at
   `proxyDistance` without gaps or doubles.
5. Hidden world objects are hidden.

## 12. Planned: cache encryption

Not part of the first version; a plan ready to implement. ChaCha20 in Enforce
Script with a key the server sends in the manifest (never stored on the client) and
a random nonce in the cache header; the cache stored as one encrypted word array.
Integrity stays with the per-batch SHA-256. `disableCache` covers maps that must not
be cached at all.

### 12.1 What it protects

Every client that receives the manifest gets the key. That reveals nothing new: the
same client can download every batch anyway. The point is that a cache file copied
off a disk (or passed around) is useless without joining the server. A generated
key's entropy is mostly the creation time (12.4); an admin-set key is the strong
option.

### 12.2 Measurements (prototype, 1.29 server, 110000 words = about 12k objects)

`TickCount()` returns microseconds (calibrated: 2000002 ticks in 2 s of UTC time).

| Step | Time |
|---|---|
| SHA-256 over the words | 147 ms |
| ChaCha20 over the words | 45 ms |
| `FileSerializer` read of one `array<int>` | 2.6 ms (write 55 ms) |
| `int.AsciiToString()` | about 65 ns per character |

For the 1229-object test maps ChaCha20 adds about 5 ms. Loading still gets faster
than today: the hash input is stored directly (12.3), so no string is read
character by character (`ToAscii()`, about 2 us per character, section 14).

### 12.3 Cache stores the hash words, not the batch

The plaintext of a batch in the cache is exactly its hash input from 10.1
(`DZOMS_BatchHasher.Words(b)`), with the per-batch type table packed 4 characters per
word. Loading hashes these words directly and decodes a batch only when its hash is
in the manifest.

The words must be kept and written back unchanged; **decoded floats must never be
re-encoded**. Simulated in float32 (Enforce `float`): `round(float(w / 1000.0) * 1000) == w`
holds for every |w| up to 8192011 (8192 m) but fails for about 9% of the values
between 8192 m and 20 km. A batch re-encoded from decoded floats would fail its hash
on the next join. Scale (`/ 10000`, values up to 20) round-trips exactly.

Decoding (`DZOMS_BatchHasher.Decode(words, start, count)` -> `DZOMS_Batch`): check
every count and string length against the remaining words before using it, names via
`AsciiToString()` per byte, positions and orientations `/ 1000.0`, scale `/ 10000.0`.
Batches whose words hash to a manifest hash are exactly what the server produced,
so the checks only guard against bugs.

### 12.4 Key and nonce

- Key: 8 ints. `cacheKey` in `config.json` (64 hex characters, optional; empty =
  generated; invalid = config error, generated key used). Otherwise generated once
  and kept in `$profile:dzo/mapstream/cachekey.bin` (`FileSerializer`, one
  `array<int>`). Deleting the file changes the key: every client downloads everything
  once more.
- Generation: mix `Math.RandomInt(0, int.MAX)` (two calls per word, 31 bits each),
  `TickCount(0)`, `GetYearMonthDayUTC`, `GetHourMinuteSecondUTC` and `g_Game.GetTime()`.
  Do **not** call `Math.Randomize(-1)`: it reseeds the game's shared generator. Not
  cryptographic quality, enough for 12.1.
- Nonce: 3 ints, made the same way on the client for every write. It only needs to be
  unique; a repeat with the same key only leaks the XOR of two cache versions to
  someone who already has the key.

### 12.5 Format and protocol

```
int        cacheVersion        // 2
int[3]     nonce               // three Write(int) calls
array<int> words               // one Write(array<int>) call, encrypted
```

Plaintext `words`: a fixed check word (`0x445A4D43`), `batchCount`, then per batch
its word count and its words (12.3). After decryption a wrong check word means a
different key: discard the file without hashing anything. One flipped word only
fails the hash of its batch.

`DZOMS_ChaCha20.XorWords(key, nonce, words)`: RFC 8439 block function, word `i` of the
data XORed with word `i` of the keystream, block counter from 0. Byte order does not
matter (only our own words are encrypted), so state words are used as they are.
Write the state as 16 locals and the 8 quarter rounds out in full (as in
`DZOMS_Sha256`); generate those lines with a script to avoid typos. Rotation left by
`n`: `(x << n) | ((x >> (32 - n)) & ((1 << n) - 1))`. Constants
`1634760805, 857760878, 2036477234, 1797285236`.

Protocol 2: `DZOMS_Manifest` gets `ref array<int> cacheKey` (8 ints, empty when
`disableCache` is set). Bump `PROTOCOL_VERSION` and `CACHE_VERSION` to 2.

### 12.6 Changes

| Where | Change |
|---|---|
| new `DZOMS_ChaCha20` (3_Game) | `Block(key, nonce, counter, dst)`, `XorWords(key, nonce, words)` |
| `DZOMS_Sha256` | hash a range of a larger array (`HashWords(words, start, count)`), no copy per batch |
| `DZOMS_BatchHasher` | `Decode()` (12.3) |
| `DZOMS_Cache` | version 2 (12.5); reads and writes per-batch word arrays, takes the key |
| `DZOMS_Manifest`, `DZOMS_Rpc` | `cacheKey`, versions 2 |
| `DZOMS_Config` + key file | `cacheKey`, generate / load the key (12.4) |
| `DZOMS_ServerWorld` | key into the manifest |
| `DZOMS_ClientWorld` | load the cache only when the manifest arrives (the key comes with it; the constructor preload in 8.1 goes away); `DZOMS_ClientBatch` keeps its words (`HandleBatch` already builds them for the hash: keep them); `Complete()` writes the stored words |
| docs | sections 4, 5, 7, 8, 10.2; `cacheKey` in the README |

### 12.7 Tests

- RFC 8439 2.3.2: key bytes `00..1f` = words `50462976, 117835012, 185207048,
  252579084, 319951120, 387323156, 454695192, 522067228`, nonce
  `150994944, 1241513984, 0`, counter 1 ->
  `e4e7f110 15593bd1 1fdd0f50 c47120a3 c7f4d1c7 0368c033 9aaa2204 4e6cd4c3
  466482d2 09aa9f07 05d7c214 a2028bd9 d19c12b5 b94e16de e883d0cb 4e3c50a2`
  (passed in the prototype).
- A partial last block (e.g. 40 words) against a Python reference; XOR twice restores.
- Cache round trip with the right key; a wrong key is rejected by the check word; one
  flipped word drops one batch.
- Positions beyond 8192 m: write -> read -> write -> read keeps all hashes.
- Key generated on the first boot, the same on the second; a set `cacheKey` is used,
  an invalid one is reported.
- Existing cache tests (truncated, wrong version, random, missing file) for version 2.

## 13. Proximity networking

Implemented. Every map object stays a local object, and the server swaps in real networked copies where
a player can use them, so that players can climb the objects (13.1). The sections below are the design and
the findings from the in-game tests (13.10a to 13.10d) that shaped it. Admin documentation is in the README.

### 13.1 Problem and findings (2026-10-04, dedicated server + 1.29 client)

Players cannot climb or vault onto local class objects. Walking, ramps and stairs on them work.
When the player tries to climb, they jump instead, hang between the lower part and the part they
want to reach (also within one object), then fall. Networked objects (a wreck made networked by
a rule) climb fine.

Measured with a debug build that logged every jump or climb press on the client and on the server
(about 100 presses, three test rounds):

- The server's climb test (`HumanCommandClimb.DoPerformClimbTest` inside the player's jump
  handling) **never returned a climb whose parent object was a local object**. Every one of the
  server's climbs (8 of 8 in the last round, and all in the earlier ones) had a parent with a
  network id; the client found climbs on local parents 16 times in the same round (at 0.4 to
  2.6 m height) and the server found nothing at the same position. The server then does a plain
  jump, client and server disagree and the player is corrected.
- The server **does have** the local objects: they are in its world (`GetObjectsAtPosition3D`),
  at the same position and orientation as on the client (to 0.001 m), and a geometry ray hits
  them. Only the climb test ignores them.
- A headless server (test players without a connection, `CreatePlayer(NULL, ...)`) finds local
  objects in the climb test like networked ones: local class objects, local and networked static
  models, and a container the production world spawned at boot. So the rule applies to connected
  players. Inferred mechanism (engine code, not readable): a climb result names its parent
  entity, which must be resolvable on both sides, so for a remote player the engine only
  accepts parents with a network id.
- **Static models do not help.** A prototype that spawned every non-networked class object as its
  static model (`CreateStaticObjectUsingP3D`, local on server and client) did not fix climbing;
  it also placed objects wrongly (13.9).
- Everyone else networks climbable objects: the Editor's own server loader creates every object
  networked (class objects without `ECE_LOCAL`, models with `createLocal = false`), vanilla
  `ObjectSpawner` creates class objects networked, Expansion and the Dabs Framework as well.

So a player can only climb what is networked, and any object can be climbed when a player reaches
a suitable surface. Networking the whole map is what this mod exists to avoid. The answer is to
network an object while a player is close enough to use it.

### 13.2 Goals

1. Every object is a local object by default, exactly as today. Login, download, caching and
   spawning are unchanged and as fast as before.
2. An object becomes a real networked object while a player who can use it is near it; it goes
   back to local when nobody needs it.
3. **Performance first, fast movers most of all.** A player in a helicopter, in a fast car or
   sprinting costs the server (almost) nothing and gets no networked objects.
4. Bounded work: per second, per frame and in total number of networked objects.
5. No gaps and no doubles on the client: the swap in both directions is never visible.

### 13.3 Which players need networked objects

Evaluated once per `updateInterval` (1 s) for every connected, living player. A player
produces **no zone** (needs nothing) or a **zone**: a disc around the player.

| Situation | Zone |
|---|---|
| on foot, speed <= `maxFootSpeed`, height above ground <= `maxHeight` | radius `networkRadius` |
| in a vehicle, speed <= `maxVehicleSpeed`, height above ground <= `maxHeight` | radius `vehicleRadius` |
| anything faster, or higher than `maxHeight` above the ground | none |
| dead, no player object | none |

- Leaving a vehicle is "on foot" from then on, so getting out of a car in a base gets that
  base networked (after the `activateDelay`).
- A zone only opens after the player has qualified for `activateDelay` (2 s) in a row, so a
  runner who stops for a moment or a car at traffic speed does not cause churn.
- Speed comes from the position change between two updates (the vehicle's speed for a
  vehicle driver, whichever the API offers; the implementation checks `GetVelocity` and
  `Transport` against `api.json` first). Height above ground is
  `position.y - SurfaceY(x, z)`.
- Defaults: `networkRadius` 150 m (40 m flickered in the prototype check, 13.10; the user
  wants 100 to 200 m on foot), `vehicleRadius` 40 m, `maxFootSpeed` 6 m/s (sprinting is faster), `maxVehicleSpeed` 8 m/s
  (about 29 km/h), `maxHeight` 20 m. All configurable (13.7).

### 13.4 Server

New class `DZOMS_Proximity`, owned by the server world, driven from `MissionServer.OnUpdate()`
with a timer (no work on frames between updates other than the queue, below).

**Data.** Built once at load, no per-update scans of all objects:
- A grid index (32 m cells) over the objects that may be promoted: every entry that is not
  already networked by a rule, whose spawn succeeded, and that no `proximityExclude` rule
  (13.7) removes. A cell holds the indices of its objects.
- Per object a state: `LOCAL` (the server's local copy exists), `NETWORKED` (a networked copy
  exists, the local copy is gone), `TO_NETWORKED` or `TO_LOCAL` (queued or in transition), and
  the time it last had a reason to be networked. The existing per-object slot of the server
  world (`m_Spawned`) refers to whichever copy exists.

**Update (once per `updateInterval`).**
1. Build the zone list from the players (13.3).
2. For every cell touched by a zone (zone radius plus one cell), for every `LOCAL` object whose
   distance to a zone centre is within that zone's radius: queue `TO_NETWORKED`, ordered by
   distance to the nearest player, nearest first.
3. For every `NETWORKED` object: if it is within the radius of any zone, refresh its "needed"
   time. Otherwise, if it is farther than `radius * 1.5` (half the radius as hysteresis) from every zone and has
   not been needed for `releaseDelay` (30 s), queue `TO_LOCAL`, farthest first. Step 3 walks
   the list of networked objects only (at most `maxNetworked`), not the map.
4. Over `maxNetworked` (default 6000): do not queue more promotions, nearest first wins; log a
   warning once per minute.

**Queue (every frame, budgeted).** At most `PROXIMITY_CREATES_PER_FRAME` promotions and
`PROXIMITY_DELETES_PER_FRAME` demotions per frame (start values 5 and 10, tuned by measurement,
13.10), promotions first. Pathgraph updates are batched: one
`ProcessMarkedObjectsForPathgraphUpdate()` per processed group, not per object.

- **Promote** (`LOCAL` -> `NETWORKED`): spawn the networked copy with the same call a rule
  networked object uses today (6.2: class objects without `ECE_LOCAL`; model objects
  `createLocal = false` followed by `SetOrientation` and `SetPosition`, 13.9), then delete the
  server's local copy, then (after `SWAP_DELAY`, 0.5 s) announce the object to the clients
  (13.5). The networked copy exists before the local one goes, so the server never has a gap.
- **Demote** (`NETWORKED` -> `LOCAL`): announce "going local" to the clients first, spawn the
  server's local copy, and delete the networked copy after `SWAP_DELAY` (the clients have their
  local copy again by then).
- A failed spawn leaves the object `LOCAL`, is counted, and is not retried for a minute.
- Objects that a rule networks from the start are not part of this and never change.

### 13.5 Protocol and client

Protocol version 2. Object ids on the wire are pairs `(batchIndex, indexInBatch)`: batches are
deterministic (6.3) and their contents never change while the digest is the same.

| Id | Direction | Payload |
|---|---|---|
| `OBJECTS_NETWORKED` | server -> all clients (broadcast) | arrays `batch[]`, `index[]`, `netLow[]`, `netHigh[]` (the network id of the networked copy) |
| `OBJECTS_LOCAL` | server -> all clients (broadcast) | arrays `batch[]`, `index[]` |

- A client that asks for the manifest also gets the full current set of networked ids, in
  `OBJECTS_NETWORKED` messages of at most 500 ids, so a joining player sees the right state.
- The client keeps a `map<int, bool>` of announced ids (key `batch * 1024 + index`; batches
  have at most 1000 objects) with at most `maxNetworked` entries.

Client behaviour (`DZOMS_ClientWorld`):
- **Spawning** (9.3, 9.4): an object whose id is in the set is not spawned as a local copy
  (the networked copy arrives through the network).
- **`OBJECTS_NETWORKED`:** add the ids to the set. For ids whose local copy exists, put the
  object on a pending list. Every update, within a budget, a pending object whose networked
  instance is present has its local copy deleted. "Present" means: the object with the announced
  network id (`GetObjectByNetworkId(netLow, netHigh)`) exists and has the same type name and
  is within 5 cm of the entry's position. The type and position search of 9.6 is **not** used:
  a hidden world object at the same place (the Editor replaced it) would pass it. After 10 s without an instance the object is dropped from the list and
  keeps its local copy (it only loses climbing), counted in the debug log.
- **`OBJECTS_LOCAL`:** remove the ids from the set. For ids in a loaded batch without a local
  copy, spawn it again (budgeted like batch spawning). The server deletes its networked copy
  after `SWAP_DELAY`; both copies have the same geometry, so the overlap is not visible.
- Unloading a batch (9.2) deletes its local copies as before and forgets pending swaps of
  that batch; the ids stay in the set.
- Distant proxies (9.6) are unchanged and apply only to rule networked objects.

Why messages and not a client-side scan: the server decides who needs what and knows what it
created; a client scan would have to match every nearby networked entity to its own data
(type and position), once per second per player, to find the same thing. Ids cost eight bytes
per object and change.

### 13.6 Fast movers and login

- A fast vehicle or aircraft makes no zone, so the server does no per-object work for it. The
  only costs are the once-per-second player evaluation and, at most, the queue of objects that
  other players or its own earlier slow phase left behind.
- A player who speeds up keeps the objects networked for `releaseDelay`; nothing is deleted
  while a car drives through the area, and nothing is created for it.
- Login: the player downloads and spawns local objects exactly as in section 8 and 9. The
  zone opens `activateDelay` after the player is alive and slow; promotion then replaces what
  is around the player, nearest first, in the background.
- A player who teleports or respawns gets a new zone there; the old area is released after
  `releaseDelay`.

### 13.7 Configuration

New fields, all with a zero value that means the default (5.2); no config migration is needed
(missing = default), the `configVersion` stays as it is.

| Field | Type | Meaning | `0` / missing means |
|---|---|---|---|
| `networkRadius` | float, m | zone radius of a player on foot (clamped 5..500) | 150 |
| `vehicleRadius` | float, m | zone radius of a slow vehicle (clamped 5..500) | 40 |
| `maxFootSpeed` | float, m/s | faster on foot: no zone | 6 |
| `maxVehicleSpeed` | float, m/s | faster in a vehicle: no zone | 8 |
| `maxHeight` | float, m | higher above the ground: no zone | 20 |
| `maxNetworked` | int | most objects networked by proximity at one time | 6000 |
| `proximityExclude` | array of rules | types that are never promoted (same pattern rules as 5.3, no `@` tokens); for performance tuning, e.g. non-solid plants | none |
| `disableProximity` | bool | switch the feature off (objects stay local, climbing on them does not work) | feature on |

With `debugLog` the server logs once per minute: players with a zone, networked objects now
(and the cap), promotions and demotions in that minute, queue length, and the time spent in
the update; and per type the number of objects promoted since start (the input for
`proximityExclude`).

### 13.8 Tests

Server, headless (test players made with `CreatePlayer(NULL, ...)`, which the climb test
accepted):
- Zone rules: on foot slow, fast, in a vehicle slow and fast, high above the ground, dead:
  zone or none, radius. Pure function over a player state record, tested with a table.
- Grid index: objects found for a zone, none outside; rule networked, failed and excluded
  objects are not in it.
- Promotion: objects near a test player become networked (non-zero network id, same type and
  position), their local copies are gone, order nearest first, budget per frame respected.
- Demotion: after the player leaves, nothing changes before `releaseDelay`, then objects are
  local again, the networked copies gone; objects near a second player stay.
- Cap: with `maxNetworked` 5 and 20 candidates, 5 nearest are promoted, one warning.
- Hashes and digest do not change with promotion; every object is in exactly one state with
  exactly one copy.
- Fast mover: a test player above `maxFootSpeed` or `maxHeight` promotes nothing.
- Config: defaults, clamping, `disableProximity`, `proximityExclude`.
- Protocol: the id lists round-trip, 500 per message, the full set on manifest request.

Client, in game (the debug logging build of 13.1 is the tool for the climb checks):
- Climb and vault onto objects that were local a moment ago, also from one part of an object
  onto another; the server finds the climb (log line pairs agree).
- No hole and no double at the swap in both directions (watch a wall while walking up to it
  and away from it); no visible pop on stairs and objects the player stands on.
- A car driven past a base at 50 km/h, a helicopter at 40 m, a sprint: no networked objects
  appear (server log counters), nothing flickers.
- Getting out of a car in the base: after `activateDelay` the base around becomes networked.
- Login time and first-minute behaviour compared with the current release.
- Zombies path around objects during swaps.

### 13.9 Also found: model objects are misplaced by `CreateStaticObjectUsingP3D` (superseded by 13.10d)

Measured on the headless server with the footprint of a downward ray scan: a model object
created with `CreateStaticObjectUsingP3D(path, pos, ori, ...)` is not where its class twin is.
The engine puts the model's bounding-box centre at `pos` and does not apply `ori` like
`SetOrientation` does: a `Land_Container_1Bo` model appeared 1.21 m too high, a greenhouse 1.32 m
high and 0.23 m aside. Calling `SetOrientation(ori)` and then `SetPosition(pos)` right after the
creation gives exactly the class object's footprint (container, greenhouse and bar tap, same
numbers). The Editor's own server loader does not do this for models.

The `.p3d` entries the mod spawns today (server and client, 6.2 and 9.4) have this problem if the
`.dze` stores the object's origin as `pos`. To decide with a real map that has model objects (the
Editor shows where it placed them); then apply the same two calls at every
`CreateStaticObjectUsingP3D` call of the mod, and to the networked copies of 13.4.

### 13.10 Prototype first (done: 13.10a to 13.10d)

The design stands on three claims that need measuring before the full implementation:
1. Promotion on a live server: the cost of creating and deleting networked objects at the
   planned rates (CPU per object, network load for a player in a dense area, time to finish a
   promotion of everything within 40 m), which sets the per-frame budgets and the default
   `maxNetworked`.
2. The swap is not visible on the client in either direction (no hole, no double, nothing pops
   under a player).
3. A promoted object can be climbed in a connected game (the probes say networked objects can).

The first implementation step is therefore a small prototype that promotes everything within
`networkRadius` of every player on foot, with the budgets as constants and the messages of 13.5,
and logs the numbers of 13.7; the user checks it in game before the rest (zone rules for
vehicles and aircraft, config fields, tests) is built.

### 13.10a Prototype check, 2026-10-04 (1.29 client, dedicated server, one player)

Build `da91b22`: every living player on foot, radius 40 m, no config.

- **Climbing works** on objects that were local before: the claim of 13.1 holds.
- **Server cost is small.** From the server log (75 status lines): 211 promotions in the first
  10 s after the player appeared, at most 466 objects networked at a time, never a queue, no
  failed promotion, 24 to 180 ms of server time per 10 s window (below 2 percent of one core).
  At 150 m the area is 14 times larger: expect several thousand objects, to be measured.
- **40 m is too small:** the swap is too visible (flicker). The default grows to 150 m; objects
  that swap far away are not seen swapping.
- **Objects vanished** after the swap, for example `StaticObj_Sidewalk*`. Likely cause (not yet
  confirmed): the client decides that the networked twin is present by type and position
  (`NetworkedInstancePresent`); the map files hide original world objects (`hidden` counts in the
  log, 60 and 23 for two maps) that the Editor replaced at the same positions. A hidden world
  object has a network id, the same type and the same position, so the client takes it for the
  twin and deletes its local copy: nothing is left. Fix: the server announces the **network id
  of the twin** (13.5) and the client looks that id up.
- **Zombies walk through non-networked walls.** Open. Not yet known whether this is new or was
  always so for local objects (the release was only checked against buildings, which the default
  rules network because of doors). To find out: the same wall with the released build, and
  with this build outside of any zone.

### 13.10b Second and third prototype checks, 2026-10-04

- **Swaps work:** 373 swaps, none timed out, no displaced twin in a dense area (client counters).
- **Zombies still walk through walls** with `experimentalNoPathgraph` (no path graph update after
  a swap) and with `experimentalKeepLocalCopy` (no object deleted). So neither the update nor the
  deletion is the cause. A control run with `disableProximity` decides whether the swapping
  matters at all. Open.
- **Model objects (`.p3d` entries) and items sit where the entry does not say.** The client logged
  231 displaced twins, almost all models (plants, rocks, crates, tire piles) and two items
  (`WaterBottle`, `FireExtinguisher`), 0.4 to 2 m off and rotated. Causes:
  - `CreateStaticObjectUsingP3D` centres the model's bounding box on the position and does not
    apply the orientation like `SetOrientation` (13.9). Reproduced on the headless server with the
    same offsets (1.96 m for the bush of the log): the **server's** models, local or networked,
    stand off their entry. Fix: `SetOrientation` and `SetPosition` after creation, on the server
    and on the client, so both sides use the entry.
  - A promoted item is simulated and settles on the ground (0.4 m up in the log); promoted
    objects now get `DisableSimulation(true)`.
  - Class objects get `SetPosition` after creation, as the Editor's loader does.
  The client logs the first three displacements per model (`model placement:` lines) so the
  client's own behaviour is visible.
- Models and the entry position: whether the Editor stores the origin or the bounding-box centre
  in `Position` for model objects is not known; the fix assumes the origin (the same meaning as for
  class objects). If a builder sees models shifted against the Editor, this is the place to look.

### 13.10c Model objects: radians, scale, networked twins (2026-10-05)

- **Orientation of model objects is in radians.** The `.dze` values of `.p3d` entries are multiples of
  pi/10 (0.314, 0.628, 1.257, -2.827); `CreateStaticObjectUsingP3D` takes radians (yaw 40 comes out as
  131.83 degrees, which is 40 rad), and the twins' degrees in the client log equal the entry times 180/pi
  (entry 0.157, twin 8.9999). Class objects use degrees. Builds 4 to 9 passed the radians to
  `SetOrientation` as degrees and turned every model almost to yaw 0. Fixed: `DZOMS_Placement.ApplyModel`.
- **`SetOrientation` and `SetYawPitchRoll` reset the scale to 1.** `SetTransform` with a scaled rotation
  matrix sets position, orientation and scale together and keeps all three.
- **Scale is not replicated.** A networked twin reaches the client with scale 1. The client gives it the
  entry's scale, and watches the announced twins (a twin that streams in after a login, or re-enters the
  network bubble, shows up unscaled again).
- **A networked model reaches the client at its creation transform**: the server's later correction is not
  replicated. The server's engine creates a model off its entry (bounding box centre, 0.1 to 10 m in the
  log); the client puts the twin at the entry (position, yaw, scale). The client's own engine creates local
  models at the entry within 2 cm, so the entry is the origin.

### 13.10d Model positions: the Editor shows the origin the engine gave (2026-10-05)

In the Editor the crate `misc_woodencrate_3x` shows `6906.566 266.867 9901.654`, orientation 18; the file
holds `6906.512 266.159 9901.661`, yaw 0.314 rad. `CreateStaticObjectUsingP3D` puts the model's **base** on
the position it is given (the origin ends up 0.7 m higher for the crate, 1.2 m for a container) and takes
radians; the Editor shows the resulting origin. So the position in the file is not the origin, and
placing the origin at the file's position (builds 4 to 10) sank models.
Rule now: the server creates a model as the file says (the engine's own placement, scale as a creation
parameter), then **rewrites the entry to the object's actual origin and yaw in degrees** and marks it
`FLAG_ORIGIN` (data entry flag 2). Clients place their local copy at that origin exactly
(`DZOMS_Placement.Apply`), respawns on the server do the same, and a networked twin (created by the same
call) already stands there. Batch data and hashes change once for maps with models.

### 13.10e Zombies right after a login (release candidate, 2026-10-05)

Driving and flying past work. Right after a login inside a mapped area, zombies that spawn run through
walls for a while; it fixes itself after some time. At a login the zone opens after 2 s and a few hundred
objects are swapped within seconds (378 in the log, 0.15 to 0.5 s of server time per minute). The
engine rebuilds the path graph for the new objects (`ECE_UPDATEPATHGRAPH`) asynchronously, while the
server deleted the old local copy, with its obstacle, at once: there was a window without any obstacle.
Now the server keeps the old copy for `OLD_COPY_DELAY_MS` (5 s) after a swap, in both directions (the
networked copy is also deleted late when an object goes back to local), so the new copy's obstacle is in
before the old one goes. The delay is a guess and a constant; a log with the zombie behaviour decides.
Also seen: the types promoted most (`debugLog` report) include plants (`bldr_plnt_*`): nobody climbs those;
`proximityExclude` is for them.

### 13.10f Several players

One zone per qualifying player; the zones add up: an object is promoted when it is inside any zone, queued
by its distance to the nearest zone (one global queue, `CREATES_PER_FRAME` per frame), and released only
when it has been outside every zone for `RELEASE_DELAY_MS` and farther than 1.5 radii from all of them.
`maxNetworked` is global (the objects nearest to any player win). The activation delay is tracked per
player by the BI id (`PlayerIdentity.GetId()`); an earlier version used the position in the player list,
which moves when players join or leave. Every swap is announced to all clients; a client that never gets
the networked copy (outside its network range) keeps its local copy and drops the swap after 10 s.

### 13.10g Zombies in an area with always networked buildings (release candidate 3)

Zombies only respect walls once the player has left an area and come back, also at buildings with doors
(networked by rule, never touched by the swaps). The path graph for the map objects seems to be built
lazily around players, so the single `ProcessMarkedObjectsForPathgraphUpdate()` after loading (server start,
nobody around) does not cover them. Experiment `experimentalPathgraphArrival` (default off): when a zone
opens, and after the player has moved 100 m, `UpdatePathgraphRegion` is called for a square of at most
100 m half extent around the player; the status line counts these updates. Remove or keep by the result.

### 13.11 Where it lives

| Where | Change |
|---|---|
| new `DZOMS_Proximity` (3_Game) | zone rules, grid index, states, update, queue (13.3, 13.4) |
| `DZOMS_ServerWorld` | owns it; promote and demote (13.4), full state on manifest request |
| `DZOMS_Rpc`, `DZOMS_Manifest` | the two messages, protocol version 2 |
| `DZOMS_ClientWorld` | id set, no local spawn for announced ids, pending swaps, local respawn (13.5) |
| `DZOMS_Config` | the fields of 13.7, normalization |
| `MissionServer` hook (5_Mission) | `OnUpdate()` drives the timer |
| `DZOMS_Placement`, `DZOMS_IdList` (3_Game) | one-step placement with `SetTransform` (13.10c); the id lists of 13.5 |
| spawning of model objects | engine placement, entries rewritten to the origin with `FLAG_ORIGIN` (13.10d) |
| docs | README (what "networked" means now, the new fields, why climbing needs it); `workshop.txt` |

## 14. Enforce Script pitfalls (verified on 1.29)

- `JsonFileLoader<T>.LoadFile()` zero-initializes missing fields (section 5.2).
- `FileSerializer` does not restore class instances stored in maps; use flat arrays.
- `EditorObjectData.EditorOnly` is read from the binary `.dze` (version >= 3); the `EDITOR_ONLY` bit in `Flags` is not the same thing and some files set it on every object.
- `Building.GetLaddersCount()` returns 0 for towers; use the `ladder<n>` selection.
- Model proxies cannot be listed from script; `IEntity.GetChildren()` is empty for
  map buildings.
- `local` and `debug` are reserved words (a config key named `debug` cannot be a member; the key is `debugLog`).
- Map objects (buildings, trees) have non-zero network ids on the server; do not use "has a network id" to tell them from spawned objects.
- `array<bool>.Resize(n)` does not initialize the new elements to false; insert them explicitly.
- Integer arithmetic wraps at 32 bits; `>>` sign-extends.
- Reading a string character by character (`Get(i).ToAscii()`) costs about 2 us
  per character; avoid it in hot paths.
- A `PrintFormat()` with nine arguments containing inline comparisons crashed the
  1.29 script compiler (whole server). Compute values into locals first.
  Also seen: one function holding `(a + b).AsciiToString()`,
  `(x == n && y == z).ToString()` and `ok = ok && v == (i & m)` segfaulted the server
  while compiling the Mission module (which of the three was not narrowed down).
- `out` is a reserved word (not usable as a parameter name).
- `TickCount(prev)` returns microseconds (calibrated against the UTC clock).
- Assigning `new` to a function parameter fails to compile ("not strong ref"):
  create objects in locals or store them in a `ref` container first.
- `Print()` cuts a line at 256 characters (the rest is lost silently): keep log lines short.
- `CreateStaticObjectUsingP3D` puts the model's bounding-box centre at the position and does not
  apply the orientation like `SetOrientation`: call `SetOrientation` and `SetPosition` after it
  (13.9).
- For a connected player the server's climb test ignores local objects (13.1). A headless test
  with `CreatePlayer(NULL, ...)` does not show this: it finds them.
- `Object.GetCollisionBox()` of class objects is not a usable footprint (zero width on one axis
  for `Land_Container_1Bo`); measure with ray casts instead.

## 15. Classes

| Module | Class | Responsibility |
|---|---|---|
| 3_Game | `DZOMS_Sha256` | word hashing |
| 3_Game | `DZOMS_Rpc` | RPC ids, protocol version |
| 3_Game | `DZOMS_Config`, `DZOMS_MapEntry` | JSON model, load, normalize, default file |
| 3_Game | `DZOMS_Glob` | pattern matching |
| 3_Game | `DZOMS_Batch`, `DZOMS_Manifest` | transfer and cache data |
| 3_Game | `DZOMS_BatchHasher` | words + hash of a batch |
| 3_Game | `DZOMS_Cache` | read / write the cache file |
| 3_Game | `DZOMS_NetworkRules` | rule lists, `@doors` / `@ladders` on `Building`, `@actions` through `DZOMS_ObjectProbe` |
| 3_Game | `DZOMS_ObjectProbe` | interface for checks that need 4_World classes |
| 4_World | `DZOMS_ObjectProbeWorld` | implements `HasOwnActions()` and `IsFixedItem()` |
| 4_World | `modded BuildingBase` | counts `AddAction()` calls into `m_DZOMS_ActionCount` |
| 3_Game | `DZOMS_BatchBuilder` | cells and batches (6.3) |
| 3_Game | `DZOMS_ServerWorld` | load maps, spawn, suppress, build batches, answer requests |
| 3_Game | `DZOMS_ClientWorld` | download, cache use, batch states, spawning, proxies, swapping local copies for networked twins (scale, model placement) |
| 3_Game | `DZOMS_Proximity`, `DZOMS_ProximityConfig`, `DZOMS_Zone`, `DZOMS_PlayerState` | zone rules, grid index, promotion and demotion queues, cap, report (section 13) |
| 3_Game | `DZOMS_IdList` | object ids (batch, index, network id) of the two proximity messages |
| 3_Game | `DZOMS_Placement` | position, orientation and scale in one step (`SetTransform`); model orientation in radians |
| 5_Mission | `DZOMS_PlayerStates` | collects the player states (position, speed, vehicle, height) for the zone rules |
| 3_Game | `modded DayZGame` | owns the server or client world, dispatches RPCs to it |
| 5_Mission | `modded MissionServer` | create the server world in `OnInit` (with a `DZOMS_ObjectProbeWorld`), rate limit cleanup in `InvokeOnDisconnect` |
| 5_Mission | `modded MissionGameplay` | create the client world in `OnInit`, update it in `OnUpdate`, destroy it in `OnMissionFinish` |
