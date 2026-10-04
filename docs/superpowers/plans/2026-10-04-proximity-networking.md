# Proximity networking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Load the `dayz-dev` skill before writing any Enforce Script: it has the no-hallucination rules, the API lookup (`api.json`; vanilla 1.29 and 1.30 scripts are in `~/workspace/dayz/mod-sources/dayz-sources`, tags `1.29` and `1.30`) and the local server test procedure this plan relies on.

**Goal:** Map objects stay local objects (fast login, cheap), but the server swaps in real networked copies near players who can use them, so players can climb them in a connected game.

**Architecture:** A server-side `DZOMS_Proximity` decides from player states which local objects to promote to networked copies (and later demote), through a budgeted queue. The server tells all clients which objects are networked with two broadcast messages (protocol 2); the client deletes its local copy once the networked twin is present, and respawns it when told the object is local again. Phase A is a deliberately crude prototype (every player on foot, constants) that the user checks in game; Phase B adds the zone rules for fast movers, config fields, cap, tuning and docs.

**Tech Stack:** Enforce Script (DayZ 1.29 and 1.30), Dabs Framework (`DF_Scripts`), the repo's headless test addon and `tools/run-server-tests.sh`, packaging with the dayz-dev skill's `dayz-mod-pack.sh`.

**Spec:** `DESIGN.md` section 13 (all of it; read it completely before Task 1), plus sections 5.2, 6, 7, 9 it extends. This plan does not contain the implementation: it fixes order, interfaces, verified facts, decisions the spec leaves open, and the tests. Where it deviates from the spec it says **Deviation** and why.

## Global Constraints

- Target DayZ 1.29 and 1.30: every task that changes scripts runs `tools/run-server-tests.sh` on both (`DZOMS_SERVER=exp` for 1.30). Use `g_Game`, never `FindFile`.
- Prefixes: classes `DZOMS_`, modded-class members `m_DZOMS_`, log lines `[dzo-mapstream]` (through `DZOMS_Log`). **`Print()` cuts a line at 256 characters**: keep every log line short.
- Unix line endings, no trailing whitespace, tabs in `.c` files; `tools/check-style.sh` passes before every commit.
- Config zero value = default (DESIGN 5.2). No config migration is needed for the new fields (`configVersion` stays 1).
- Networked copies use the flags of rule networked objects (DESIGN 6.2), in particular `ECE_NOPERSISTENCY_WORLD` and `ECE_NOPERSISTENCY_CHAR`: promoted objects must never end up in the persistence.
- Never edit or write into the Steam directories; tests run in server trees from `make-server-tree.sh` on ports from `free-ports.sh`. Never run two servers on one tree.
- Uploading to the Workshop is the user's call: build and test the package, then ask; when the user says "upload it", run `workshop --no-prompt update --content "pack/build/@dzo_mapstream" --ignore-file "pack/.workshopignore" --glob '!*.biprivatekey' -m "<message>"` from the repo root (it needs `--no-prompt`, there is no TTY).
- Enforce pitfalls: DESIGN section 14, and the list in `docs/superpowers/plans/2026-10-01-dzo-mapstream.md` (no ternary, no duplicate local names per function, `ref` only on members, no `new` assigned to parameters, test seams must not need constructors with parameters).
- Plans carry no full implementation; short snippets only where a detail is easy to get wrong.

## Verified facts this plan relies on

- Networked objects can be climbed in a connected game; local objects cannot (DESIGN 13.1, from the probe logs of 2026-10-04). The server's climb test only accepted parents with a network id.
- The headless server can create players without a connection: `g_Game.CreatePlayer(null, "SurvivorM_Mirek", pos, 0, "NONE")` returns a `Human` (used by the probe tests). They are good test players for zone and promotion logic; their climb behaviour is **not** representative.
- `g_Game.CreateObjectEx(type, pos, flags)` without `ECE_LOCAL` gives a non-zero network id (`Object.GetNetworkID(low, high)`); with `ECE_LOCAL` it gives 0/0.
- Objects spawned synchronously in a unit test are visible to ray casts and `GetObjectsAtPosition3D` immediately; no ticks are needed.
- `CreateStaticObjectUsingP3D` misplaces objects (DESIGN 13.9): the fix is `SetOrientation(ori)` then `SetPosition(pos)` after creation. Verified on container, greenhouse and bar tap by ray footprints (`tests/.../Test_Collision.c` on branch `probe/climb-log`).
- `MissionServer.OnUpdate(float timeslice)` is already overridden by the mod (`DZOMS_MissionServer.c`, 60 s rate-limit sweep timer); the world objects are `GetDayZGame().DZOMS_GetServerWorld()` and `DZOMS_GetClientWorld()`.
- RPC plumbing: `DZOMS_Rpc` has ids `0x445A4D01..04` (`FIRST`/`LAST`), `DayZGame.OnRPC` dispatches by id (`DZOMS_DayZGame.c`); the server sends with `ScriptRPC.Send(null, id, true, sender)`; a broadcast uses `sender = null`.
- Batches: `DZOMS_BatchBuilder.Build(objects, batchSize, outBatches, outBounds)` puts object indices into batches in its own order; today nothing records which batch and which index inside the batch an object ended up at. Task 1 adds that.
- The debug build that logs every jump and climb press (client and server) is on branch `probe/climb-log`: `dzo_mapstream/scripts/4_World/DZOMS_ClimbProbe.c`. Do not merge the branch; copy that one file into the prototype build with `git checkout probe/climb-log -- dzo_mapstream/scripts/4_World/DZOMS_ClimbProbe.c` (Task 5) and remove it again before Phase B.
- The probe tests (`Test_Collision.c`, scenario `climbprobe`) stay on the probe branch.

## Review Focus

1. **A player dies, disconnects or leaves the server mid-zone:** the zone must disappear on the next evaluation and the objects must be released by the normal delay, not stay networked forever. Pinned in Task 4 (zone list built from live players only) and Task 8.
2. **A swap under a player's feet** (standing on or climbing an object that is promoted or demoted): the server must create the new copy before deleting the old one in both directions. Pinned in Task 4 (order of operations in the promote and demote tests); the visual part is an in-game item of Task 5.
3. **A demotion whose local respawn fails on the server:** the networked copy must stay, the object stays `NETWORKED`, and the retry is delayed. Pinned in Task 4/8.
4. **Messages for ids a client cannot use** (batch not received, index out of range, duplicates, the same id in both messages, a batch that is unloaded): ignore or bound them, never throw, never grow the id set past `maxNetworked`. Pinned in Task 3.
5. **A client joins during a promotion wave:** it must get the full current set, and an `OBJECTS_LOCAL` for an id it never saw must be harmless. Pinned in Tasks 2 and 3.
6. **Server restart:** nothing promoted may persist; after a restart the objects are local again and nothing is duplicated. In-game item of Task 5 (restart the server once and look).

---

## Phase A: prototype (does promotion work in a connected game?)

Branch `feature/proximity` from `design/proximity-networking`. Phase A is functionally complete enough to be useful but crude: **every living player on foot** gets a zone of 40 m, the budgets are constants, there is no config, no speed or height rule, no cap. It ends with a build for the user's in-game check; **stop there and wait for the verdict**.

### Task 1: object to batch mapping

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_BatchBuilder.c`, `dzo_mapstream/scripts/3_Game/DZOMS_ServerWorld.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_BatchBuilder.c`, `Test_ServerWorld.c`

**Interfaces:**
- Produces: `DZOMS_BatchBuilder.Build(...)` gets two more output arrays, `array<int> outObjectBatch` and `array<int> outObjectIndex`, parallel to the input `objects` (batch number and position inside that batch for input object `i`). `DZOMS_ServerWorld` keeps them and exposes `int GetObjectBatch(int objectIndex)` and `int GetObjectIndexInBatch(int objectIndex)` (-1 for an invalid index) and `int GetObjectCount()`.

- [ ] **Step 1: Write the failing tests:** in `Test_BatchBuilder`: every input object appears exactly once; `outObjectBatch`/`outObjectIndex` point at a batch entry with the same type string, position and orientation as the input object; the arrays have the input's length; unchanged batch contents and hashes compared with the old signature's output on the existing fixtures (determinism). In `Test_ServerWorld`: after `Load()` the accessors resolve every entry of `GetObjects()` to the matching entry of `GetManifest()`'s batches (compare `GetBatches()` entries), `-1` for out-of-range indices.
- [ ] **Step 2: Run** `tools/run-server-tests.sh unit` — expect failures (compile errors count; read `script_*.log`).
- [ ] **Step 3: Implement** (record the mapping where `Build` appends objects to a batch; the server world calls the new signature and stores the arrays). Update every existing caller of `Build` (grep).
- [ ] **Step 4: Run** all scenarios on 1.29 and then `DZOMS_SERVER=exp` on 1.30; digests must equal the pre-change digests (the `determinism` scenario prints them).
- [ ] **Step 5: Commit** (`Batches: remember where each object ended up`).

### Task 2: protocol 2 messages and server broadcast

**Files:**
- Modify: `DZOMS_Rpc.c`, `DZOMS_DayZGame.c`, `DZOMS_ServerWorld.c`; create `dzo_mapstream/scripts/3_Game/DZOMS_IdList.c`
- Test: `Test_Requests.c`, a new `Test_Proximity.c` (registered in `DZOMS_TestSuites.RunAll()`)

**Interfaces:**
- Produces:
  - `DZOMS_Rpc.OBJECTS_NETWORKED = 1146768645` and `OBJECTS_LOCAL = 1146768646` (`LAST` becomes ...646); `PROTOCOL_VERSION = 2` (`CACHE_VERSION` stays 1: the batch data and hashes do not change).
  - `class DZOMS_IdList` (flat payload: `ref array<int> batch`, `ref array<int> index`; `Count()`, `Add(b, i)`, `Write(ScriptRPC)`, `static bool Read(ParamsReadContext ctx, DZOMS_IdList out)` that rejects mismatched lengths and counts above 500).
  - Server: `void DZOMS_ServerWorld.BroadcastNetworked(DZOMS_IdList ids)` and `BroadcastLocal(DZOMS_IdList ids)` (chunks of at most 500 ids, `Send(null, id, true, null)`), `ref array<bool> m_Networked`-like state exposure: `bool IsPromoted(int objectIndex)` and `void MarkPromoted(int objectIndex, bool value)` (state kept per object index, size = object count). On a manifest request the server also sends the full set of promoted ids to that identity (same chunking, `Send(..., sender)`).

- [ ] **Step 1: Write the failing tests:** `DZOMS_IdList` write/read round trip through a `ScriptRPC` is not possible headless, so test it through `FileSerializer` or the existing payload-test approach in `Test_ClientDownload.c` (look at how `DZOMS_Manifest` round trips are tested and copy that); reject unequal lengths and 501 ids; `PROTOCOL_VERSION` 2 and a client with version 1 gets no manifest (existing wrong-protocol case in `Test_Requests`); `MarkPromoted`/`IsPromoted` bounds (invalid index ignored, no throw); the dispatcher ids: `DZOMS_Rpc.LAST` covers both new ids.
- [ ] **Step 2: Run** unit — fail.
- [ ] **Step 3: Implement** the constants, `DZOMS_IdList`, the server send helpers and the manifest-request extension. `DZOMS_DayZGame.OnRPC`: the two new ids are server -> client only, route them to the client world (`OnRpc`, Task 3), ignore them on the server.
- [ ] **Step 4: Run** all scenarios on 1.29 and 1.30, check-style.
- [ ] **Step 5: Commit** (`Protocol 2: object ids for networked copies`).

### Task 3: client handling of announced ids

**Files:**
- Modify: `DZOMS_ClientWorld.c`
- Test: `Test_ClientSpawn.c`, `Test_Proximity.c`

**Interfaces:**
- Consumes: Task 2 (`DZOMS_IdList`, rpc ids), Task 1 is server-only.
- Produces in `DZOMS_ClientWorld`: `map<int, bool> m_NetworkedIds` (key `batch * 1024 + index`), `void HandleNetworked(DZOMS_IdList ids, int nowMs)` and `void HandleLocal(DZOMS_IdList ids, int nowMs)` (called from `OnRpc` for the two new ids; also directly by tests), `int GetNetworkedIdCount()`, `bool IsAnnouncedNetworked(int batch, int index)`, `int GetPendingSwapCount()`, `int GetSwapTimeoutCount()`. Constants: `SWAP_TIMEOUT_MS = 10000`, id key factor 1024, id set cap (taken from the server: stop adding above 4000 ids and ignore the rest, counted).

Behaviour (DESIGN 13.5):
- Spawning (`SpawnStep`, the local spawn of a batch entry) skips entries whose id is announced.
- `HandleNetworked`: validate every id (batch in range and received, index in range of that batch, entry not already rule networked); add to the set; for an entry with a live local copy put it on a pending list. `Update` (existing per-frame function, within a budget of 20 entries per call) checks pending entries with `NetworkedInstancePresent(type, pos, localCopy)` (existing, 9.6): present -> delete the local copy, clear the slot, drop from pending; older than the timeout -> drop from pending, keep the local copy, count it.
- `HandleLocal`: remove the ids from the set (unknown ids are ignored); for an entry in a `LOADED` or `LOADING` batch without a local copy, spawn it again (queue it like batch spawning, budgeted); remove it from pending.
- Unloading a batch forgets its pending entries; the ids stay in the set. A batch loaded later skips announced entries.

- [ ] **Step 1: Write the failing tests** (client code runs on the headless server in the existing `Test_ClientSpawn`; follow how it builds a client world with a fake batch and calls `Update`): announced id before spawn -> not spawned locally; announced after spawn with a networked instance created at the same position -> local copy deleted (the instance is created in the test with `CreateObjectEx` without `ECE_LOCAL`); announced without an instance -> after the timeout the local copy is still there and `GetSwapTimeoutCount()` is 1; `HandleLocal` -> local copy back, id gone from the set, pending cleared; invalid ids (batch out of range, index out of range, duplicates, an id in both messages in one tick, `HandleLocal` for an unknown id) cause no error and no change; the set never exceeds the cap; unloading and reloading a batch honours the set.
- [ ] **Step 2: Run** unit — fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** all scenarios on 1.29 and 1.30, check-style.
- [ ] **Step 5: Commit** (`Client: swap local copies for announced networked objects`).

### Task 4: server promotion and demotion (prototype rules)

**Files:**
- Create: `dzo_mapstream/scripts/3_Game/DZOMS_Proximity.c`
- Modify: `DZOMS_ServerWorld.c` (owns it; promote/demote primitives), `dzo_mapstream/scripts/5_Mission/DZOMS_MissionServer.c` (drives it), `DZOMS_Log` usage only
- Test: `Test_Proximity.c`

**Interfaces:**
- Consumes: Task 1 accessors, Task 2 `Broadcast*`, `IsPromoted`/`MarkPromoted`.
- Produces:
  - `class DZOMS_PlayerState` (plain record: `vector pos`, `float speed` (m/s), `bool inVehicle`, `float height` (above ground), `bool alive`, `int id`); the prototype ignores everything except `pos` and `alive`.
  - `DZOMS_Proximity` (no constructor parameters; `void Init(DZOMS_ServerWorld world)` builds the grid index), with:
    - `void Update(array<ref DZOMS_PlayerState> players, int nowMs)`: builds zones, queues promotions and demotions (DESIGN 13.4, prototype: zone radius `NETWORK_RADIUS = 40`, one for every alive player, release after `RELEASE_DELAY_MS = 30000` and `HYSTERESIS = 20`), at most once per `UPDATE_INTERVAL_MS = 1000`;
    - `void ProcessQueue(int nowMs)`: at most `CREATES_PER_FRAME = 5` promotions and `DELETES_PER_FRAME = 10` demotions, promotions first, nearest/farthest first as in the design; the pending announcements (`SWAP_DELAY_MS = 500`) are sent from here, one `OBJECTS_NETWORKED`/`OBJECTS_LOCAL` broadcast per call at most;
    - counters for the debug log: `int GetNetworkedCount()`, `int GetQueueLength()`, `int GetPromotions()`, `int GetDemotions()`, `int GetFailures()`.
  - `DZOMS_ServerWorld`: `bool Promote(int objectIndex)` (spawn the networked copy exactly like a rule networked object, class objects without `ECE_LOCAL`, model objects `createLocal = false` plus `SetOrientation` and `SetPosition`; **then** delete the local copy; replace the slot; mark promoted; false on a failed spawn, nothing changed) and `bool Demote(int objectIndex)` (spawn the local copy first, **then** delete the networked copy after the announce delay, via a pending list handled in `ProcessQueue`; false when the local spawn fails: nothing changes).
  - Objects that a rule networked from the start (entry has `FLAG_NETWORKED`), objects with a failed spawn (NULL slot) and `.p3d` entries of a missing model are not in the index.
  - Hook: `MissionServer.OnUpdate` gathers the player states from `g_Game.GetPlayers()` (alive `PlayerBase`, `GetPosition()`), calls `Update` and `ProcessQueue` each frame; the state gathering is a separate small function so it can be replaced in Phase B.
- **Decision (ruling for the implementer):** the `Update` interval and the pending list run on the `nowMs` value passed in, never on `g_Game.GetTime()` inside the class, so unit tests can drive time.

- [ ] **Step 1: Write the failing tests** in `Test_Proximity.c` with a server world built from a fixture map (`DZOMS_TestFixtures`, a handful of local class objects spread over 200 m at known positions, plus one rule networked object and one failed-spawn object): objects within 40 m of a test player's position are promoted after `Update` + enough `ProcessQueue` calls: non-zero network id, same type and position (5 cm), the old local object deleted (`GetSpawned()` slot is the new one, the old pointer is gone), `IsPromoted` true; order nearest first and never more than the per-frame budget per `ProcessQueue` call; the rule networked and the failed object untouched; an object just outside 40 m is not promoted; a dead player produces no zone; a second `Update` within the interval does nothing; moving the player 100 m away changes nothing before `RELEASE_DELAY_MS`, then demotes the farthest first, local copy present before the networked one is deleted (check both exist right after the first `ProcessQueue` of the demotion and only one after the announce delay), `IsPromoted` false; two players: objects near the second stay promoted; a failed local respawn (use a type that cannot be spawned: temporarily replace the entry's type in the test) keeps the networked copy, `GetFailures()` grows, no throw; hashes and `GetDigest()` of the server world do not change through promotion and demotion; the announced ids arrive in `OBJECTS_NETWORKED`/`OBJECTS_LOCAL` chunks (capture through a test seam: the broadcast helpers write to an overridable method or a recording list; keep the seam free of constructor parameters).
- [ ] **Step 2: Run** unit — fail.
- [ ] **Step 3: Implement** `DZOMS_Proximity` (grid index 32 m cells over promotable entries, built once in `Init`; zone query by cells; a list of promoted indices for the demotion pass; the queue as two int arrays sorted on insert), the two `DZOMS_ServerWorld` primitives and the `MissionServer` hook. Batch pathgraph updates: one `g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate()` per `ProcessQueue` call that created or deleted anything.
- [ ] **Step 4: Run** all scenarios on 1.29 and 1.30, check-style.
- [ ] **Step 5: Commit** (`Server: promote local objects near players to networked copies`).

### Task 5: package for the in-game check

**Files:** none new; temporary `DZOMS_ClimbProbe.c` copy (uncommitted or in a commit labelled `PROTOTYPE` that Phase B removes).

- [ ] **Step 1:** add the debug logging: `git checkout probe/climb-log -- dzo_mapstream/scripts/4_World/DZOMS_ClimbProbe.c`; it needs `DZOMS_ClientWorld.GetBatchCount()`/`GetClientBatch()` and `DZOMS_ServerWorld.GetSpawned()`, which exist. Boot-test it on both versions. Commit as `PROTOTYPE: climb logging`.
- [ ] **Step 2:** server log lines for the prototype (through `DZOMS_Log`, once per 10 s while anything changed): networked now, queue length, promotions, demotions, failures and the milliseconds `Update` plus `ProcessQueue` took in that period (`TickCount`, microseconds). Short lines.
- [ ] **Step 3:** `P=~/.claude/skills/dayz-dev/scripts/dayz-mod-pack.sh; $P -C pack build && $P -C pack check`, then `DZOMS_PACKAGE=1 tools/run-server-tests.sh` on 1.29 and with `DZOMS_SERVER=exp`.
- [ ] **Step 4: Ask the user** to upload (print the command; run it when they say "upload it"), restart the server, and check in game with a client:
  1. climb and vault onto objects that were local a moment ago (hood to cabin, walls, containers, fences, furniture), and from one part of an object onto another; send both script logs (`climb-probe` lines: the server's parent must now have a network id);
  2. no hole and no double at the swap in both directions: walk up to a wall and away from it, stand next to a container while it swaps, stand on a low object while others around it swap; any pop or flicker;
  3. zombies still path around objects during swaps;
  4. login: time until the area around the player is complete, compared with the last release; the first minute after login;
  5. server numbers from the log: promotions per second at the start (landing in a dense base), update milliseconds, FPS if visible;
  6. restart the server once: nothing duplicated, the first start after has no networked leftovers (objects local again);
  7. any object that fails to promote (server warnings).
- [ ] **Step 5: Stop.** Record the answers in DESIGN 13.10 (date, versions, numbers) and commit that. Negative on 1 (still not climbable): the design is void, discuss. Problems on 2 or 5: tune the budgets/`SWAP_DELAY_MS` first. Positive: Phase B.

---

## Phase B: the real rules (only after a positive check)

Same branch. Remove the prototype copy of `DZOMS_ClimbProbe.c` first (`git rm`, commit `Remove climb logging build`) unless the user wants to keep it for the final check.

### Task 6: zone rules for fast movers

**Files:** `DZOMS_Proximity.c`, `DZOMS_MissionServer.c` (state gathering), `Test_Proximity.c`

**Interfaces:**
- Consumes: Task 4.
- Produces: `static DZOMS_Zone DZOMS_Proximity.ZoneFor(DZOMS_PlayerState p, DZOMS_ProximityConfig c)`, a pure function (`class DZOMS_Zone`: `bool active`, `float radius`) implementing the table of DESIGN 13.3 (on foot slow, vehicle slow, faster, high, dead); `class DZOMS_ProximityConfig` (plain record of the fields of DESIGN 13.7, filled from `DZOMS_Config` in Task 7, constants as defaults here). `activateDelay` (2 s of qualifying in a row) tracked per player id inside `Update`.
- Real player states: look up in `api.json`/`dayz-sources` (tags 1.29 and 1.30) what gives the speed (candidates: `GetVelocity(Object)` or the position change between updates; for vehicle drivers `Transport`/`CarScript.GetSpeedometer()` — verify each before use), the vehicle test (`player.IsInTransport()` or `GetParent()`), and the height (`pos.y - g_Game.SurfaceY(x, z)`); pick what exists on both versions, otherwise `#ifdef DAYZ_1_29`.

- [ ] **Step 1: Write the failing tests:** a table over `ZoneFor` with states: walking 1.5 m/s -> 40; jogging 5 -> 40; 6.1 on foot -> none; sprint 7 -> none; vehicle 2 m/s -> 15; vehicle 8.1 -> none; helicopter at 50 m height -> none whatever the speed; at 15 m height and 3 m/s in a vehicle -> 15; on foot 19 m above ground (a roof) -> 40; 21 m -> none; dead -> none; custom config values change the result. A player who becomes slow only for 1.5 s gets no zone, for 2 s a zone (`activateDelay`); a runner who passes through a base promotes nothing; a car at speed over objects promotes nothing and demotes nothing.
- [ ] **Step 2: Run** unit — fail. **Step 3: Implement.** **Step 4: Run** all scenarios on both versions. **Step 5: Commit** (`Proximity: zones by speed, height and vehicle`).

### Task 7: configuration

**Files:** `DZOMS_Config.c`, `DZOMS_ServerWorld.c`/`DZOMS_Proximity.c` (read them), `Test_Config.c`, `Test_Proximity.c`

**Interfaces:**
- Produces on `DZOMS_Config` (DESIGN 13.7): `float networkRadius, vehicleRadius, maxFootSpeed, maxVehicleSpeed, maxHeight`, `int maxNetworked`, `ref array<string> proximityExclude`, `bool disableProximity`; defaults 40, 15, 6, 8, 20, 4000, none, false; radii clamped 5..200 with a warning like the existing distances; `maxNetworked` clamped 1..20000; `DZOMS_ProximityConfig.FromConfig(DZOMS_Config)`.
- `proximityExclude` uses the pattern rules of 5.3 without `@` tokens: an `@` token or empty pattern is a config error (logged, ignored) like in `DZOMS_NetworkRules.Parse`; types matching are not in the grid index.

- [ ] **Step 1: Write the failing tests:** the defaults on a missing file and on `{ "maps": [] }`; each field clamped or defaulted; `proximityExclude` excludes a type by name and by glob, an `@doors` entry is reported; `disableProximity` true: `Init` builds nothing, `Update` promotes nothing, objects stay local; an existing version 1 `config.json` without the new fields loads with the defaults and is not rewritten (no migration, no `.bak`).
- [ ] **Step 2: Run** unit — fail. **Step 3: Implement** (the default config file written for a new install stays as it is: do not list the new fields). **Step 4: Run** all scenarios on both versions. **Step 5: Commit** (`Config: proximity networking options`).

### Task 8: cap, backoff, release rules and the debug report

**Files:** `DZOMS_Proximity.c`, `DZOMS_ServerWorld.c`, `Test_Proximity.c`

- [ ] **Step 1: Write the failing tests:** `maxNetworked` 5 with 20 candidates near one player: the 5 nearest are promoted, no more, exactly one warning in the log per minute (count through a log seam or the counter `GetCapHits()`); when the player moves, the cap frees as objects are demoted and new ones near the player are promoted nearest first; an object whose promotion failed is not retried for 60 s (`GetFailures()` stops growing) and is retried after; a demotion whose local respawn failed keeps the object `NETWORKED` and retries after 60 s (Review Focus 3); a player who disconnects (removed from the state list) or dies: the zone vanishes at the next `Update`, objects are released after `RELEASE_DELAY_MS` (Review Focus 1); an object between the radius and radius + hysteresis is neither promoted nor demoted; with `debugLog` the once-per-minute line carries players with a zone, networked now and cap, promotions and demotions of that minute, queue length, update time, and per type the promoted counts (short lines, each under 256 characters; assert the length of every produced line).
- [ ] **Step 2: Run** unit — fail. **Step 3: Implement.** **Step 4: Run** all scenarios on both versions. **Step 5: Commit** (`Proximity: cap, retry delays and debug report`).

### Task 9: model object placement fix

**Files:** `DZOMS_ServerWorld.c` (`SpawnOnServer`), `DZOMS_ClientWorld.c` (`SpawnLocal`), `Test_ServerWorld.c`, `Test_ClientSpawn.c`

DESIGN 13.9. **Ask the user first** for a `.dze` with model objects (or a map where they know a model object's intended spot), and where the Editor shows it, because whether the mod's current `.p3d` placement is wrong depends on what the file stores; do not change it without that check.

- [ ] **Step 1:** with a real model object: spawn it the old way and with `SetOrientation` + `SetPosition` and compare with the Editor's placement (user check, or the footprint scan of `Test_Collision.c` on the probe branch against the class twin if the model has one). Record the result in DESIGN 13.9.
- [ ] **Step 2: Write the failing test** (a model object spawned by the server world and by the client spawn function ends up with `GetPosition()` equal to the entry position within 5 cm and the footprint of its class twin, using the ray scan helper copied from the probe branch). **Step 3: Implement** at every `CreateStaticObjectUsingP3D` call (server local, networked copies of Task 4, client). **Step 4: Run** all scenarios on both versions. **Step 5: Commit** (`Model objects: set orientation and position after creation`).

### Task 10: documentation and release candidate

**Files:** `DESIGN.md`, `README.md`, `workshop.txt`, `DEVELOPMENT.md`

- [ ] **Step 1:** DESIGN: fold section 13 into sections 3 (terms: promoted object), 5.2 (the new fields), 6 (6.5 proximity), 7 (protocol 2 messages), 9 (client swap, 9.7), 11 (tests), 15 (classes); replace section 13 by a short "why proximity networking" paragraph (13.1 findings and the 256-character `Print` fact stay in section 14 and 6).
- [ ] **Step 2:** README: what "networked" and "local" mean now (objects are local until a player is close, then networked), why (climbing needs a networked object), the new settings table rows and how to tune (`networkRadius`, `proximityExclude` for plants, `disableProximity`), server load notes (what is created and how many per second), update note (protocol 2: server and clients must run the same version; nothing in `config.json` changes).
- [ ] **Step 3:** `workshop.txt` in the same words (Steam BBCode).
- [ ] **Step 4:** DEVELOPMENT in-game list: the client items of DESIGN 13.8.
- [ ] **Step 5:** `tools/check-style.sh`; full tests on both versions, also `DZOMS_PACKAGE=1`; **commit**.
- [ ] **Step 6:** `publish-hint` / ask the user; on "upload it" run the update command; ask the user to run the DESIGN 13.8 client list (climb, no hole or double, fast car and helicopter, getting out of a car, login time, zombies, a server restart) and record the result in DEVELOPMENT.md.
