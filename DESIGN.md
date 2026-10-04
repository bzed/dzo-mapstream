# dzo_mapstream: design

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
cache encryption (planned, section 12), static objects instead of local class
objects (planned, section 13).

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

## 13. Planned: static objects instead of local class objects

Not implemented yet; a plan ready to implement once the gate test (13.2) passes. It
replaces the local class objects of sections 3, 6.2 and 9.4: afterwards every object
is either **networked** (a class object the server creates) or **static** (a model
the server and every client create with `CreateStaticObjectUsingP3D`). Local class
objects (`CreateObjectEx` with `ECE_LOCAL`) are no longer used for map objects, only
for the distant proxies (9.6) and the probe on the server (6.2).

### 13.1 Problem (found in game)

Players cannot climb or vault onto local class objects. Walking, ramps and stairs
on them work. When the player tries to climb, they jump instead, hang between the
lower part and the part they want to reach (also within one object, e.g. from the
hood of a wreck up to its cabin), then fall, sometimes through the lower part.
Verified with `HMMV_Wreck`: local, it cannot be climbed; made networked by a rule,
the same object can.

Cause, inferred (the climb test is engine code, `HumanCommandClimb.DoPerformClimbTest`):
the climb result `SHumanCommandClimbResult` stores the grab and stand points relative
to parent entities (`m_GrabPointParent`, `m_ClimbStandPointParent`,
`m_ClimbOverStandPointParent`), and the climb runs on the client and on the server.
A local class object is two unrelated entities, one per side, without a network id,
so client and server cannot agree on the parent; the server's correction makes the
player fall. Walking needs no reference to an object, so it is not affected.

Nobody else uses local class objects for map content. Vanilla `ObjectSpawner`
(cfggameplay `objectSpawnersArr`, runs on the server only) creates class objects
networked and `.p3d` objects with `CreateStaticObjectUsingP3D(..., createLocal = true)`;
Expansion's `ExpansionWorldObjectsModule.SpawnObject` and the Dabs Framework
`EditorObjectData.CreateObject` create networked objects.

### 13.2 Gate: static objects must climb in multiplayer

The only climbing test with static objects so far ran in the DayZ Editor, i.e.
offline, without a separate client and server; local class objects climb fine there,
too. Before implementing anything:

1. In a test map, place the `HMMV_Wreck` model (its `CfgVehicles HMMV_Wreck model`)
   as a model object, next to a networked `HMMV_Wreck`.
2. Dedicated server plus one client with this mod: vault and climb onto the model
   (also hood -> cabin), walk on it, compare with the networked one.
3. Check that the client sees **one** model, not two. The server spawns `.p3d`
   objects with `createLocal = true` and every client spawns its own copy; if the
   server's copy reached the clients through the network, every model would be
   doubled. (`.p3d` objects already take this path today, so this checks the
   current code as well.)

If static objects cannot be climbed either, this plan is void; the fallback is to
network everything that should be climbable, at the cost this mod exists to avoid.

### 13.3 Spawn modes

New config field `spawnMode`, globally and per map file:

| Field | Type | Meaning | `""` / missing means |
|---|---|---|---|
| `spawnMode` | string | `"static"` or `"editor"` (case-insensitive) | `"static"` |
| `maps[].spawnMode` | string | the same for one map file | the global value |

An unknown value is a config error: logged, the field is treated as missing.

What happens to each object (rules = the networked rules of 5.3, with the
additions of 13.4):

| Object in the `.dze` | `"static"` (default) | `"editor"` |
|---|---|---|
| class object, a rule makes it networked | networked | networked |
| class object, otherwise | static, with the class's model (13.6) | networked |
| class object without a model | networked, logged (`no model`) | networked |
| model object (`.p3d`) | rules decide; not networked = static | the same |

- `"static"` is for maps built from class objects, which is how the Editor places
  everything by default. Only what needs the network (rules) is networked.
- `"editor"` takes the map builder's choice from the Editor: the Editor can turn an
  object into a static object (stored as its `.p3d` path in the `.dze`) and back
  into a class object. Class objects are networked without looking at the rules;
  the debug report shows `(editor)` as their rule.
- The meaning of the rules does not change, only the name of the other outcome:
  a matching `!pattern` now makes an object **static** instead of local. Logs,
  README and report say "static".
- A class object that becomes static loses everything its class adds to the model:
  script behaviour (lights, particles, sounds, actions), damage and ruins, and
  retextures (13.4). The default rules keep the known cases networked; anything
  else needs a rule or `"editor"` mode.

### 13.4 Retextured classes: `@retextured`

Many classes share one model and only differ in `hiddenSelectionsTextures` /
`hiddenSelectionsMaterials` (colour variants of containers, wrecks, signs).
`CreateStaticObjectUsingP3D` only takes a model, so the static copy would show the
model's own textures. New rule token:

| Rule | Matches |
|---|---|
| `@retextured` | class objects whose `CfgVehicles <type>` has a non-empty `hiddenSelectionsTextures` or `hiddenSelectionsMaterials` array |
| `!@retextured` | negated form: such objects become static with the model's own textures |

- Decided from the config (`g_Game.ConfigGetTextArray`), no probe object needed.
  Verify that inherited values are returned (a colour variant usually inherits
  `hiddenSelections` from its base class and only sets the textures).
- `.p3d` objects never match.
- Measure on the test maps how many types it selects. If base classes set these
  arrays to the model's own textures, it selects too much; then compare the
  textures with the base class before counting a class as retextured.
- The default config gets `@retextured` after `@fixedItems`. Existing
  `config.json` files get it through the config migration to version 2 (13.5).

### 13.5 Config version and migration

`config.json` gets a version, so later releases can add options or change the
meaning of existing ones and bring old files up to date instead of guessing.

| Field | Type | Meaning | `0` / missing means |
|---|---|---|---|
| `configVersion` | int | layout version of this file; written by the mod, admins do not edit it | version 1 (a file from before versioning) |

- `DZOMS_Config.CURRENT_VERSION = 2` with this plan. Version 1 is the layout of
  section 5 as released; it has no `configVersion` field, which is why `0`
  means 1 (zero value = default, 5.2).
- The default config (5.6, missing file) is written with `configVersion` =
  `CURRENT_VERSION`.

Loading, extending 5.6 (after `LoadFile()` succeeded, before `Normalize()`):

1. `v = configVersion`, `0` -> `1`.
2. `v == CURRENT_VERSION`: nothing to do.
3. `v > CURRENT_VERSION` (file of a newer mod version): log an error, use the
   file as it is (fields this version does not know are ignored by
   `JsonFileLoader`), never write it.
4. `v < CURRENT_VERSION`: apply the migration steps `v -> v+1` in order, each one
   a static function on the loaded, not yet normalized config
   (`MigrateTo2(DZOMS_Config c)`, ...), then set `configVersion = CURRENT_VERSION`.
   Then write the file:
   1. Back up the admin's file: `CopyFile(path, path + ".v<v>.bak")` (an existing
      backup of that name is deleted first with `DeleteFile`). If the backup fails,
      log an error and **do not write** the migrated file; the migrated config is
      used in memory for this start, and the next start tries again.
   2. `JsonFileLoader<DZOMS_Config>.SaveFile(path, c)`; a failure is logged, the
      migrated config is still used.
   3. Log one line per step, e.g.
      `[dzo-mapstream] config: migrated config.json from version 1 to 2 (added @retextured), backup config.json.v1.bak`.
5. Normalize as before. The migrated file is written **before** normalizing, so
   fields that were `0` (= default) stay `0` in the file and keep following future
   default changes.

This is the one exception to "never overwrite an admin's file" in 5.6, and only
for a valid file of an older version; broken JSON is still never written.
Rewriting goes through the config class, so the file loses what the class does not
know (misspelled keys) and its formatting; the backup keeps the original.

**Migration 1 -> 2:** insert `"@retextured"` at the **start** of the global
`networked` list, unless some rule in it already contains `@retextured` (any case,
also `!@retextured`). At the start, every existing admin rule comes later and still
wins (last match wins, 5.3), so an admin's `!Land_Container_1Bo` keeps that type
static. Per-map lists are not touched: they come after the global list anyway.
`spawnMode` is not added: missing means `"static"`, the new default.

Rules for later versions: a new option whose zero value is its default needs no
migration (missing = default), only the version bump if the file should show it;
a change of meaning or of a default that old files must not silently get needs a
migration step. Every step gets a test with a file of the old version.

### 13.6 Server: spawning and data

Decision, extending 6.2 (`SpawnEntry`):

1. `.p3d` types: unchanged.
2. Class types in `"editor"` mode: networked, no rule evaluation.
3. Class types in `"static"` mode: rules as today (including the probe spawn for
   `@doors`, `@ladders`, `@actions`, `@fixedItems`; the probe object is always
   deleted afterwards). Networked: spawn networked. Otherwise look up the model
   (`DZOMS_TypeName.ModelPath`, cached per type); empty: spawn networked and count as
   `no model`; else spawn static with `CreateStaticObjectUsingP3D(model, pos, ori,
   scale, true)`.
4. Cache the result per (type name, rule list, spawn mode) as today, including the
   model path.

The data entry of a static object carries the **model path as its type**, exactly as
if the `.dze` had contained the model. Clients already spawn `.p3d` types as static
objects (9.4), so the client's spawn code does not change, and the hash (10.1)
covers the model. All batch hashes change once with this version, so every client
downloads the maps once more; protocol and cache format stay the same.

- Model path string: the `model` config value is written like `\DZ\...\x.p3d`; pass it
  in the same form that `.p3d` entries of `.dze` files have (look at one when
  implementing; `ModelPath()` lower-cases and turns `\` into `/`, which is for
  matching, not for spawning). The gate test (13.2) uses the same string.
- Duplicates (6.1 step 4) and rules use the class name, as now; the conversion
  happens after both.
- Pathgraph: class objects used `ECE_UPDATEPATHGRAPH`. Vanilla calls
  `g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate()` once after its object
  spawner; do the same once after all maps are loaded. In-game test: zombies walk
  around static objects instead of through them.
- Distant proxies (9.6) only exist for networked objects and stay class objects.

### 13.7 Report

The per-map line counts `networked` and `static` (instead of `local`), plus
`no model`. With `debugLog`, networked types list the deciding rule as before, or
`(editor)` or `(no model)`; static objects converted from classes are not listed one
by one, only counted.

### 13.8 For map builders (README text, to be written with the implementation)

The README gets a section "Building maps for this mod" with this content:

- By default every object becomes a **static object**: the plain model, solid and
  climbable, created by each player's game. Doors, ladders, wells, levers, placed
  targets and recoloured objects are kept as normal (networked) objects
  automatically.
- A static object only has its model. Lights that switch on, fire and smoke,
  sounds, things that can be destroyed, and actions of the object are gone. If an
  object needs any of this, make it networked: add its type to `networked` in
  `config.json` (its class name, or a pattern like `"*Lamp*"`), or use the Editor mode below.
- Recoloured objects (same model, other texture) stay networked so they keep their
  colour. If the plain model colour is fine, add `"!@retextured"` (or
  `"!<type>"` for single types) to save the network load.
- **Editor mode** (`"spawnMode": "editor"`, for one map in `maps[]` or globally):
  the map is spawned exactly as built. In the Editor, objects can be switched
  between "static object" and "config object". Static objects stay static, every
  config object becomes networked. Use this when you want to decide per object;
  keep the number of config objects low, every one of them costs network traffic
  for every player near it.
- Climbing, vaulting and walking work on static and networked objects alike.

The README sections "What it does", "Networked rules" and the config table, and
`workshop.txt`, are updated to say "static" instead of "local". The config table
gets `configVersion` ("written by the mod, do not change it"), and the update notes
say: on the first start of this version the server adds `@retextured` to the
`networked` list, rewrites `config.json` and keeps the old file as
`config.json.v1.bak`.

### 13.9 Changes

| Where | Change |
|---|---|
| `DZOMS_Config`, `DZOMS_MapEntry` | `spawnMode` global and per map, normalized (13.3); `@retextured` in the default `networked` list; `configVersion`, `CURRENT_VERSION = 2`, migration with backup and write-back, `MigrateTo2` (13.5) |
| `DZOMS_NetworkRules` | `@retextured` / `!@retextured` (13.4); "local" -> "static" in names and messages |
| `DZOMS_TypeName` | spawnable model path for a class (13.6), next to `ModelPath()` |
| `DZOMS_ServerWorld` | decision 13.6, entry type = model path for static objects, `ProcessMarkedObjectsForPathgraphUpdate()` after loading; `SpawnOnServer` no longer creates local class objects except the probe |
| report (`DZOMS_MapReport`) | `static`, `no model`, `(editor)` (13.7) |
| `DZOMS_ClientWorld` | nothing for spawning; the class branch of `SpawnLocal` remains for proxies only |
| docs | DESIGN sections 3, 5, 6.2, 9.4, 11 rewritten from this section, then this section removed; README (13.8), `workshop.txt` |

### 13.10 Tests

Server, headless:

- Config version: a missing file is written with `configVersion` 2. A version 1
  file (no `configVersion`) with a custom `networked` list is migrated:
  `@retextured` first, the other rules unchanged and in order, `configVersion` 2,
  backup `config.json.v1.bak` identical to the original, `0` fields still `0` in
  the written file. A version 1 file that already has `!@retextured` only gets the
  version. A version 2 file is not written (unchanged modification time). A version
  3 file logs an error, is used as it is and is not written. Broken JSON is not
  written (as before). A second start after a migration changes nothing.
- `spawnMode`: missing, `""`, `"static"`, `"STATIC"`, `"editor"`, unknown (config
  error, default); per-map value overrides the global one, `""` inherits.
- Static mode: a class without rule becomes a batch entry with its model path and
  no networked flag; a networked rule keeps it a class with the flag; a class
  without `model` becomes networked and is counted as `no model`.
- Editor mode: every class object is networked whatever the rules say; `.p3d`
  objects still follow the rules.
- `@retextured`: a colour variant matches (with inherited `hiddenSelections`), its
  base class without textures does not, `!@retextured` overrides, `.p3d` never
  matches.
- The probe object of `@doors` & co. is gone after loading: no object without a
  network id left at a static entry's position except the static object itself.
- Hashes are stable over two starts and change when `spawnMode` changes.

Client, in game:

- Climb and vault onto static objects converted from classes (`HMMV_Wreck`, walls,
  containers), also from one part of an object onto a higher part.
- Every static object appears once (no network copy on top, 13.2).
- Recoloured objects keep their colour (networked) and show the model colour with
  `!@retextured`.
- Zombies path around static objects.
- An `"editor"` map: Editor static objects are static, config objects networked.

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
| 3_Game | `DZOMS_ClientWorld` | download, cache use, batch states, spawning, proxies |
| 3_Game | `modded DayZGame` | owns the server or client world, dispatches RPCs to it |
| 5_Mission | `modded MissionServer` | create the server world in `OnInit` (with a `DZOMS_ObjectProbeWorld`), rate limit cleanup in `InvokeOnDisconnect` |
| 5_Mission | `modded MissionGameplay` | create the client world in `OnInit`, update it in `OnUpdate`, destroy it in `OnMissionFinish` |
