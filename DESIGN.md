# dzo-mapstream: design

Loads DayZ Editor map files (`.dze`) on the server and transfers them to the clients
as plain data instead of thousands of networked objects. The server and every
client spawn the objects locally. Objects that only work when networked (doors,
ladders, levers, ...) are spawned as networked objects by the server.

Target: DayZ 1.29 and 1.30. Status: design, nothing implemented yet.

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
    "debugLog": false
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
- Model objects (`.p3d`): `CreateStaticObjectUsingP3D(path, pos, ori, scale, createLocal)`.
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

Payload classes contain only ints, floats, bools, strings, vectors and arrays of
those. **No maps and no nested class instances**: `FileSerializer` does not restore
class instances stored inside maps (verified on 1.29: they come back as `NULL`),
while flat arrays round-trip bit-exactly through `FileSerializer` and `ScriptRPC`.

```c
class DZOMS_Manifest
{
    int protocolVersion;            // 1
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

Not part of the first version. ChaCha20 in Enforce Script with a key the server
sends in the manifest (never stored on the client) and a random nonce in the cache
header; the cache stored as one encrypted word array. Integrity stays with the
per-batch SHA-256. `disableCache` covers maps that must not be cached at all.

## 13. Enforce Script pitfalls (verified on 1.29)

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
- Assigning `new` to a function parameter fails to compile ("not strong ref"):
  create objects in locals or store them in a `ref` container first.

## 14. Classes

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
| 3_Game | `DZOMS_ClientWorld` | download, cache use, batch states, spawning, proxies |
| 3_Game | `modded DayZGame` | owns the server or client world, dispatches RPCs to it |
| 5_Mission | `modded MissionServer` | create the server world in `OnInit` (with a `DZOMS_ObjectProbeWorld`), rate limit cleanup in `InvokeOnDisconnect` |
| 5_Mission | `modded MissionGameplay` | create the client world in `OnInit`, update it in `OnUpdate`, destroy it in `OnMissionFinish` |
