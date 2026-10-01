# dzo-mapstream Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Load the `dayz-dev` skill before writing any Enforce Script: it has the no-hallucination rules, the API lookup (`api.json`) and the local server test procedure this plan relies on.

**Goal:** A DayZ 1.29/1.30 mod that loads DayZ Editor `.dze` maps on the server, spawns them there, and streams them to clients as hashed, cacheable batches that clients spawn locally, closest first.

**Architecture:** Almost all logic is in the 3_Game script module (the RPC entry point `DayZGame.OnRPC` lives there). A server world loads config and maps, spawns every object (networked or local by readable rules), groups objects into deterministic batches and answers rate-limited RPC requests. A client world downloads the manifest and batches (verified by SHA-256 against the manifest), caches them per server, and spawns/unloads them by camera distance within per-frame budgets. 4_World only supplies a probe for checks that need `BuildingBase`/`ItemBase`; 5_Mission only holds the mission hooks.

**Tech Stack:** Enforce Script (DayZ 1.29), Dabs Framework (`DF_Scripts`, workshop id 2545327648), Mikero `makepbo`/`extractpbo` (in `/usr/local/bin`), local headless DayZ Server (Steam app 223350) via the dayz-dev skill scripts, Python 3 for reference vectors only.

**Spec:** `DESIGN.md` (repo root). Read it completely before Task 1. Every task names the sections it implements. This plan does not contain the implementation: it fixes interfaces, the decisions the spec leaves open, verified API facts, and the tests. Where it deviates from the spec it says **Deviation** and why.

## Global Constraints

- Target DayZ 1.29 (v1.29.163709) and 1.30. Use `g_Game`, not `GetGame()`. Never use `FindFile` (broken on 1.30).
- `CfgPatches` class `dzo_mapstream`, `requiredAddons[] = {"DZ_Data", "DF_Scripts"};`, `config.cpp` as text (makepbo `-B`), PBO prefix `dzo_mapstream`, script modules `dzo_mapstream/scripts/{3_Game,4_World,5_Mission}`. Loaded with `-mod=` on server and clients.
- Every script class prefixed `DZOMS_`; members added to a `modded class` prefixed `m_DZOMS_`, methods added to modded classes prefixed `DZOMS_`. Every log line starts with `[dzo-mapstream]`.
- Unix line endings, no trailing whitespace, tabs in `.c` files.
- Payload classes (`DZOMS_Batch`, `DZOMS_Manifest`) contain only ints, floats, bools, strings, vectors and arrays of those.
- RPC ids `1146768641..1146768644` (= `0x445A4D01..04`): `REQUEST_MANIFEST`, `MANIFEST`, `REQUEST_BATCH`, `BATCH`. Protocol version 1, cache version 1. Write ids as decimals.
- Config zero value = default: `viewDistance` 0 → 1500, `proxyDistance` 0 → 1000, `batchSize` 0 → 400 (clamp 50..1000).
- Limits: server ≤ 1 manifest/identity/s, ≤ 20 batches/identity/s (token bucket, capacity 20, refill 20/s). Client: one batch request in flight, 5 s timeout, 30 s pause after 5 consecutive timeouts, manifest re-request every 3 s; distances every 250 ms; ≤ 100 spawns and ≤ 200 deletes per frame; proxy pass every 1 s, ≤ 50 networked objects per frame.
- Enforce pitfalls (DESIGN.md §13 plus these, verified in vanilla/Dabs sources):
  - no ternary operator; `local` is reserved; ints are 32-bit and wrap; `>>` sign-extends; parenthesize every bitwise expression;
  - the same local variable name must not be declared twice in one function, even in sibling blocks;
  - `ref` only on member variables; never assign `new` to a function parameter;
  - no nine-argument `PrintFormat` with inline comparisons; build log strings from locals;
  - `JsonFileLoader` zero-initializes fields missing from the file;
  - hex literals above `0x7FFFFFFF` are not safe: write such constants as signed decimals;
  - whether a subclass constructor forwards arguments to a base constructor with parameters is **not verified**: design test seams so that tests never need to subclass a class whose constructor takes parameters (use return codes, public flags, or plain-data inputs instead).
- Never write into the Steam directories. Tests run in a server tree built by `make-server-tree.sh`, on random ports from `free-ports.sh` (dayz-dev skill `testing/local-server.md`).

## Review Focus

Implied by the spec, not fully exercised by any single test; each line names the task that pins it. Reviewers check these first.

1. **`.dze` object with `Scale` 0 or missing** (JSON `.dze` without `Scale` loads as 0): must spawn and be transmitted with scale 1, not invisible. Task 10.
2. **RPC payload size**: a 1000-object batch with long `.p3d` paths is ~60 KB. Headless tests cannot show whether `ScriptRPC` delivers that. The in-game test (Task 14) must run once with `batchSize: 1000`; if it fails, lower the clamp maximum, do not invent chunking.
3. **Garbage from cache or network**: a batch with unequal array lengths or an out-of-range `typeIndex` must never reach the hasher or the spawner. `IsValid()` before hashing everywhere. Tasks 3, 4, 12.
4. **Identity churn on the server**: identities that disconnect before a player entity exists never reach the disconnect hooks, so rate-limit entries need an idle sweep. Task 11 (**Deviation** from §6.4).
5. **Same type probed twice for `@actions`**: vanilla `BuildingBase.InitializeActions()` caches the action map per script type and only calls `SetActions()` for the first instance, so counting via `InitializeActions()` returns 0 on the second probe. Task 8 (**Deviation** from §5.3 wording).

---

## Verified facts (DayZ 1.29 api.json, vanilla scripts.pbo, Dabs Framework Scripts.pbo)

Re-check anything not listed here with `api.json` (dayz-dev skill) before using it.

**Vanilla API (signatures as in 1.29):**
- `CGame`: `proto bool GetHostAddress(out string address, out int port)`, `proto native Object CreateObjectEx(string type, vector pos, int iFlags, int iRotation = RF_DEFAULT)`, `proto native Object CreateStaticObjectUsingP3D(string p3dFilename, vector position, vector orientation, float scale = 1.0, bool createLocal = false)`, `proto native void ObjectDelete(Object obj)`, `proto native Object GetObjectByNetworkId(int low, int high)`, `proto native vector GetCurrentCameraPosition()`, `proto native void GetObjectsAtPosition3D(vector pos, float radius, out array<Object> objects, out array<CargoBase> proxyCargos)` (null allowed for the cargo list), `GetObjectsAtPosition(...)` (same, 2D), `proto int GetTime()` (ms), `proto bool ConfigGetText(string path, out string value)`, `int ConfigGetInt(string path)`, `ConfigGetChildrenCount(string path)`, `ConfigGetChildName(string path, int index, out string name)`, `SurfaceY(float x, float z)`, `IsDedicatedServer()`.
- `DayZGame`: `DayZGameState GetGameState()` with `DayZGameState.IN_GAME`; `override void OnRPC(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)`. Vanilla's `OnRPC` calls `super`, then `Event_OnRPC.Invoke(...)` (Dabs listens there), then target/no-target handling. Null-target RPCs from clients reach the server's `DayZGame.OnRPC` (CF and Dabs rely on this).
- `Object`: `SetOrientation(vector)`, `GetOrientation()`, `GetPosition()`, `string GetType()`, `proto native owned string GetShapeName()`, `proto void GetNetworkID(out int low, out int high)`, `proto native void GetSelectionList(out TStringArray)`; `IEntity`: `SetScale(float)`, `GetScale()`, `int Update()`.
- `Building`: `GetDoorCount()`, `GetLaddersCount()` (useless for towers, §5.3). `EntityAI.CanPutIntoHands(EntityAI parent)`.
- `BuildingBase` (4_World/Entities/Game/Super/Building.c): `void InitializeActions()` uses a static per-type map and calls `SetActions()` only if no map is cached for the type; `void SetActions()`; `void AddAction(typename actionName)`; `void RemoveAction(typename actionName)`; both use `m_InputActionMap`, which is NULL until `InitializeActions()` ran.
- `MissionServer`: `override void OnInit()`, `override void OnUpdate(float timeslice)`, `void InvokeOnDisconnect(PlayerBase player)` (only called when the player entity still exists, no uid), `void PlayerDisconnected(PlayerBase player, PlayerIdentity identity, string uid)` (always gets `uid == identity.GetId()`; identity may already be deleted). `MissionGameplay`: `OnInit()`, `OnUpdate(float timeslice)`, `OnMissionFinish()`.
- `PlayerIdentityBase.GetId()`. `ScriptRPC.Send(Object target, int rpc_type, bool guaranteed, PlayerIdentity recipient = NULL)`; `Serializer.Write/Read` return bool. `FileSerializer.Open(string path, FileMode mode)`, `Close()`.
- `JsonFileLoader<T>.LoadFile(string filename, out T data, out string errorMessage)` and `SaveFile(string filename, T data, out string errorMessage)` (both bool; pass an already created instance to `LoadFile`).
- `FileExist`, `DeleteFile`, `MakeDirectory`, `OpenFile`, `FPrint`, `CloseFile`; `int FGets(FileHandle, string var)` returns -1 at the end and 0 for an empty line: loop with `> 0`.
- `string`: `ToLower()` (in place), `Replace()` (in place), `Substring(start, len)`, `IndexOf`, `LastIndexOf`, `Contains`, `Get(i)`, `ToAscii()`, `TrimInPlace()`, `Length()`. `Math.Round/Floor` return float (assigning to int truncates; vanilla does `int x = Math.Round(f)`). `Math.Clamp` takes floats only. `Math.Max/Min/AbsFloat`. `vector.Distance`, `vector.DistanceSq`. `bool.ToString()` exists. `~x` works on ints.
- Vector literals as strings work: `const vector X = "0 1 0";`, `pos + "0 30 0"`.
- The `ECE_*` flags the spec uses all exist (`ECE_LOCAL`, `ECE_SETUP`, `ECE_CREATEPHYSICS`, `ECE_UPDATEPATHGRAPH`, `ECE_NOLIFETIME`, `ECE_NOPERSISTENCY_WORLD`, `ECE_NOPERSISTENCY_CHAR`).

**Dabs Framework (workshop 2545327648, `addons/Scripts.pbo` + `GUI.pbo`; `DF_Scripts` requires `DZ_Scripts` and `DF_GUI`, both in that mod):**
- `EditorSaveData.LoadFromFile(string file)` → `EditorSaveData` or NULL; reads both binary (`"EditorBinned"` header) and JSON. Fields `EditorObjects` (`array<ref EditorObjectData>`), `EditorHiddenObjects` (`array<ref EditorDeletedObjectData>`). `void Write(Serializer, int version)` writes the binary format (`EditorSaveData.Version` = 8) — use it to create test fixtures.
- `EditorObjectData`: `string Type; vector Position; vector Orientation; float Scale = 1; EditorObjectFlags Flags; [NonSerialized()] bool EditorOnly`. `EditorObjectFlags.EDITOR_ONLY = 128`.
- `EditorDeletedObjectData`: `Object FindObject(float radius = 0.1)` (matches by shape name, falls back to type), `static EditorDeletedObjectData Create(Object)`.
- `GetDayZGame().GetSuppressedObjectManager()` → `SuppressedObjectManager` with `SuppressMany(notnull array<Object>, bool inform_clients = true)`, `UnsupressMany(...)` (Dabs spelling), `IsSuppressed(Object)`. Dabs' `MissionServer.InvokeOnConnect` sends the list to each connecting client. Check whether the manager can be NULL at `MissionServer.OnInit()` (`3_Game/DabsFramework/!Core/DayZGame.c`, around line 30); handle NULL with an error log.

**Tools:** `makepbo -B -P -@=<prefix> <dir> <out.pbo>` produces a PBO with text `config.cpp` and `$PBOPREFIX$`; `extractpbo -P <pbo> <dir>` to inspect. Skill scripts: `find-dayzserver.sh [-v|-w]`, `make-server-tree.sh <tree>` (prints the tree path), `free-ports.sh 3` (three ports, space separated). The Steam `serverDZ.cfg` uses mission `dayzOffline.chernarusplus`.

---

## File Structure

```
mod.cpp, .gitignore (/build/), README.md
dzo_mapstream/config.cpp
dzo_mapstream/scripts/3_Game/
  DZOMS_Log.c            Info/Warn/Err with the [dzo-mapstream] prefix
  DZOMS_Rpc.c            RPC ids, protocol and cache version
  DZOMS_Sha256.c         SHA-256 over 32-bit words (+ byte front end for tests)
  DZOMS_Batch.c          DZOMS_Batch, DZOMS_Manifest
  DZOMS_BatchHasher.c    batch -> words -> hash; digest over all hashes
  DZOMS_Cache.c          cache file read/write
  DZOMS_Glob.c           glob matcher
  DZOMS_TypeName.c       type name, model path, normalization
  DZOMS_Config.c         DZOMS_Config, DZOMS_MapEntry
  DZOMS_ObjectProbe.c    probe base class
  DZOMS_NetworkRules.c   DZOMS_Rule, DZOMS_NetworkRules
  DZOMS_BatchBuilder.c   DZOMS_ObjectEntry, DZOMS_BatchBuilder
  DZOMS_RateLimiter.c
  DZOMS_ServerWorld.c    (+ DZOMS_MapReport)
  DZOMS_ClientWorld.c    (+ DZOMS_ClientBatch)
  DZOMS_DayZGame.c       modded DayZGame
dzo_mapstream/scripts/4_World/
  DZOMS_BuildingBase.c, DZOMS_ObjectProbeWorld.c
dzo_mapstream/scripts/5_Mission/
  DZOMS_MissionServer.c, DZOMS_MissionGameplay.c
tests/dzo_mapstream_tests/            test addon (prefix dzo_mapstream_tests), never shipped
  config.cpp
  scripts/5_Mission/DZOMS_T.c, DZOMS_TestMission.c, DZOMS_TestUtil.c, DZOMS_TestFixtures.c, Test_*.c
tests/python/sha_vectors.py
tools/build.sh, tools/check-style.sh, tools/run-server-tests.sh
```

## How tests work (read before Task 1)

Enforce has no test framework, so the plan uses a test addon:

- `tests/dzo_mapstream_tests` is a second PBO (`requiredAddons[] = {"dzo_mapstream"}`, only a Mission module). Its `modded class MissionServer.OnInit()` (a) prepares scenario files, (b) calls `super.OnInit()` — which runs the real mod's hook — (c) checks the production world for the scenario, (d) runs every test suite, (e) prints `[dzo-mapstream-test] DONE pass=<n> fail=<m>`. Failures print `[dzo-mapstream-test] FAIL <suite>: <what> got '<x>' want '<y>'`.
- `DZOMS_T` provides `Suite`, `Check(bool, what)`, `Eq(string got, string want, what)`, `EqInt`, `EqFloat` (tolerance 0.001), `Log`, `Done`, `Scenario()` (reads `$profile:dzo-mapstream-tests/scenario.txt`), and `DIR = "$profile:dzo-mapstream-tests"`. `DZOMS_TestSuites.RunAll()` lists the suites; each task adds one line.
- `DZOMS_TestUtil`: `TEST_ORIGIN = "1000 500 14000"` (empty air in north-west Chernarus; each suite uses its own offset from it so suites never overlap), `SpawnLocal(type, pos)`, `Delete(obj)`.
- `tools/run-server-tests.sh [scenario...]` builds both PBOs, (re)creates a server tree (default `~/dayz-testserver-dzoms`, env `DZOMS_TREE`), symlinks `@DabsFramework` (workshop dir from `find-dayzserver.sh -w`), `@dzo-mapstream` and `@dzo-mapstream-tests` into it, and per run: takes 3 fresh ports from `free-ports.sh`, sets `steamQueryPort` in the tree's `serverDZ.cfg` and an RCon seed `profiles/battleye/beserver_x64.cfg` (random password; delete stale `beserver_x64_active_*.cfg`) as `testing/local-server.md` describes, clears old logs, writes `scenario.txt`, starts `./DayZServer -config=serverDZ.cfg -profiles=profiles "-mod=@DabsFramework;@dzo-mapstream;@dzo-mapstream-tests" -port=<game> -nosplash -nopause -dologs` from the tree under `ulimit -c 0` and `timeout -k 15 <RUNTIME>` in the background, polls the newest `profiles/script_*.log` for a ready pattern, kills the server when it appears, copies the log to `build/last-<scenario>.log`, and fails on any `SCRIPT.*(E)` line, a missing `DONE`, or `fail` ≠ 0.
- "Run the tests" means `tools/run-server-tests.sh unit` unless a task says otherwise; one scenario takes about a minute.
- TDD red state in Enforce is usually a **compile error** naming the class or method the test uses. Read it and confirm it names what you are about to add.

---

### Task 1: Mod skeleton, build, test harness

Implements DESIGN.md §2, §11 server test 1.

**Files:** create `mod.cpp`, `.gitignore`, `dzo_mapstream/config.cpp`, `3_Game/DZOMS_Log.c`, `3_Game/DZOMS_Rpc.c`, `5_Mission/DZOMS_MissionServer.c`, the test addon (`config.cpp`, `DZOMS_T.c`, `DZOMS_TestMission.c`, `DZOMS_TestUtil.c`, `Test_Smoke.c`), `tools/build.sh`, `tools/check-style.sh`, `tools/run-server-tests.sh`.

**Interfaces produced:** `DZOMS_Log.Info/Warn/Err(string)` (Warn adds `WARNING: `, Err adds `ERROR: `); `DZOMS_Rpc.REQUEST_MANIFEST/MANIFEST/REQUEST_BATCH/BATCH/FIRST/LAST/PROTOCOL_VERSION/CACHE_VERSION`; the test harness above; the mission hook prints `[dzo-mapstream] ready` (Task 11 changes it to `ready: digest <hex>`).

**Decisions:**
- `config.cpp` declares only the Game and Mission modules for now (`dependencies[] = {"Game", "Mission"}`); Task 8 adds the World module together with its first file (an empty module folder is not packed).
- `CfgMods` follows the layout in the dayz-dev skill `systems/mod-structure.md`.
- `tools/build.sh` writes `build/@dzo-mapstream/{mod.cpp,addons/dzo_mapstream.pbo}` and `build/@dzo-mapstream-tests/addons/dzo_mapstream_tests.pbo`.
- `tools/check-style.sh` fails on CR characters or trailing blanks in tracked and untracked `.c .cpp .sh .md .py .json` files.
- Scenario `boot`: one run with `-mod=@DabsFramework` (no ready pattern, waits the full runtime), one with `@DabsFramework;@dzo-mapstream` (waits for `[dzo-mapstream] ready`); asserts no script errors and that the file counts of the Game and Mission modules increased. Look at the real log line once (`grep -h 'Module:' build/last-boot.log`) and parse the count from it; the skill describes it as `Module: Game; loaded ...`.
- Scenario `unit`: config `{ "maps": [] }`, all suites.

- [ ] **Step 1:** Verify prerequisites: `makepbo`/`extractpbo` present, `find-dayzserver.sh -v` reports `StateFlags: 4`, Dabs Framework present in the workshop dir. If something is missing, stop and ask the user (no steamcmd).
- [ ] **Step 2:** Write `Test_Smoke` (RPC ids equal `1146768641` and `1146768644`, `LAST - FIRST == 3`, `g_Game.IsDedicatedServer()`), the harness and the tools. Run `tools/run-server-tests.sh unit`; expect a compile error naming `DZOMS_Rpc`.
- [ ] **Step 3:** Add `DZOMS_Log`, `DZOMS_Rpc`, the mission hook, `config.cpp`, `mod.cpp`. Inspect the PBO once with `extractpbo -P` (prefix, text `config.cpp`, script paths).
- [ ] **Step 4:** `tools/check-style.sh && tools/run-server-tests.sh boot unit` → both `ok`, `unit` `fail=0`.
- [ ] **Step 5:** Commit ("Add mod skeleton, build script and headless test harness").

---

### Task 2: SHA-256 over 32-bit words

Implements DESIGN.md §10.1 (`DZOMS_Sha256` and its implementation notes).

**Files:** `3_Game/DZOMS_Sha256.c`, `tests/python/sha_vectors.py`, `Test_Sha256.c`.

**Interfaces produced:** `static string DZOMS_Sha256.HashWords(array<int> words)` → 64 lower-case hex chars, the hash of the words written as 4 bytes big endian each; `static string HashBytes(array<int> bytes)` (values 0..255, tests only); `static int Shr(int x, int n)` (logical shift, 1 ≤ n ≤ 31).

**Decisions:**
- One compression function over pre-padded 16-word blocks; two padders: words (append `1 << 31`, zeros until count ≡ 14 mod 16, then the bit length as high word `(n >> 27) & 31` and low word `n << 5`) and bytes (0x80, zeros until ≡ 56 mod 64, 64-bit big-endian bit length, then pack 4 bytes per word).
- Round constants and initial hash values as **signed decimal** ints: let `sha_vectors.py` print them from the standard tables and paste the output; do not type hex above `0x7FFFFFFF`.
- `rotr(x, n) = ((x >> n) & ((1 << (32 - n)) - 1)) | (x << (32 - n))`, inlined in the round loop with the mask as a literal (calls per round are slow in Enforce; the spec's ~20 ms for 1229 objects depends on it). Hex output nibble: `(x >> shift) & 15`.

**Tests (expected values from `python3 tests/python/sha_vectors.py`, checked against hashlib):**

| Input | SHA-256 |
|---|---|
| words `[]` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| words `[1633837824]` (0x61626300) | `dc1114cd074914bd872cc1f9a23ec910ea2203bc79779ab2e17da25782a624fc` |
| words 1..1 | `b40711a88c7039756fb8a73827eabe2c0fe5a0346ca7e0a104adc0fc764f528d` |
| words 1..13 | `8a86417e98eb2cbe494407851dc18351326f155d54e1099475c120babe9dfd4b` |
| words 1..14 | `dd4821522337506fa727f0c8de421dfb5be9f9c43b868da32704869feee2b93b` |
| words 1..15 | `a623c527f0cb3f474b35fce6c0cc885aadfd48db31180aab5046b9f86e6cc676` |
| words 1..16 | `dbd03ea5a0ea94144594b68ec08063af1ae32b7598a2e4f5654c9fa7786efc42` |
| words 1..17 | `b356e65c1a376b9bcf7cc8994e70323dd823699e1e67ef610df2f63ab4bdce7c` |
| words `[-1, 1<<31, 2147483647, 0]` | `2981f521c4ff2baf45679be9b4460d211114bcbe1ff54230eb50a324542eb450` |
| words `(i*7919) & 0x7fffffff`, i = 0..999 | `b81c1ebb6bffb62fc252449cea079ace627a985b242997cd0338995e84dbcb03` |
| bytes `""` | same as words `[]` |
| bytes `"abc"` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |
| bytes `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"` | `248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1` |
| bytes 55 × `a` | `9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318` |
| bytes 56 × `a` | `b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a` |
| bytes 64 × `a` | `ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb` |

Plus `Shr(-1, 28) == 15`, `Shr(1 << 31, 31) == 1`, `Shr(256, 4) == 16`. If only the padding edge cases (13/14/16 words, 55/56/64 bytes) fail, the padding is wrong, not the rounds.

- [ ] Python script → failing test → implementation → tests pass → commit ("Add SHA-256 over 32-bit words").

---

### Task 3: Payload classes and batch hashing

Implements DESIGN.md §7 (classes), §10.1 (hash input), Review Focus 3.

**Files:** `3_Game/DZOMS_Batch.c`, `3_Game/DZOMS_BatchHasher.c`, extend `sha_vectors.py`, `Test_Hasher.c`.

**Interfaces produced:**
- `DZOMS_Batch` with exactly the spec's fields (`index`, `types`, `typeIndex`, `flags`, `positions`, `orientations`, `scales`; arrays created in the constructor), `FLAG_NETWORKED = 1`, `int Count()`, `bool IsNetworked(int i)`, `string GetType(int i)`, `int Add(string type, vector pos, vector ori, float scale, int flags)` (reuses the type slot, returns the entry index), `bool IsValid()` (no NULL array, equal lengths, every `typeIndex` in range, no empty type).
- `DZOMS_Manifest` with exactly the spec's fields (constructor sets `protocolVersion = PROTOCOL_VERSION` and creates the arrays), `int Count()`, `bool IsValid()` (arrays present, `bounds.Count() == 4 * hashes.Count()`), `vector GetCenter(int i)`, `float GetRadius(int i)`.
- `DZOMS_BatchHasher`: `static void AppendString(array<int> words, string s)` (length, then 4 chars per word, big endian, zero padded), `static array<int> Words(DZOMS_Batch)`, `static string Hash(DZOMS_Batch)` (caller guarantees `IsValid()`), `static string Digest(array<string> hashes)` (count + `AppendString` of each hash, hashed; used to compare server runs).

**Reference batch** (floats are exact binary fractions so float32 and Python agree): types `["Land_Lighthouse", "dz\\plants\\tree\\t_piceaabiesf.p3d"]` (single backslashes in the actual string, 32 chars), entries:
1. type 0, flags 1, pos `1000.25 50.5 2000.75`, ori `90 0 0`, scale 1
2. type 1, flags 0, pos `1010 51 2010`, ori `-45.5 1.25 0`, scale 1.5
3. type 1, flags 0, pos `1020.125 52 2020`, ori `0 0 0`, scale 0.75

Expected words: `2 15 1281453668 1598843239 1752459375 1970496768 32 1685740656 1818324596 1935438962 1701141620 1601202531 1700880738 1768256358 779105124 3 0 1 1000250 50500 2000750 90000 0 0 10000 1 0 1010000 51000 2010000 -45500 1250 0 15000 1 0 1020125 52000 2020000 0 0 0 7500`

| Variant | Hash |
|---|---|
| reference (also with `index = 17`: index is not hashed) | `15f1a247ecbe2f7f46a7c93c4203c1cb4c36bd63b472a0b9681a471b754fbe60` |
| entry 3 moved to x = 1020.140625 | `0d5bf3838a0fecb163d4eec622693ff15f56b3b195f348958e5a5039218a2622` |
| entry 2 flags = 1 | `dc3e6317c41f072552c33a2261681ce1dbd23e08f4bca631544dbda0003cd466` |
| type 0 renamed `Land_Lighthousf` | `cf0a7bea5b0f25aff94cdc43d9371155c8d0279aa56192691b2164c051c6a551` |
| entry 3 removed | `71dbc1aab42d2bad7b674c0cac371a9b3c2386ec0f8a28e4f95929be72961cb3` |

Python reference rounding: `floor(|f| + 0.5) * sign(f)` (Enforce `Math.Round` rounds halves away from zero).

**Further tests:** `IsValid()` false for one shorter array, `typeIndex == types.Count()`, a negative `typeIndex`, an empty type string, a NULL array. Manifest with 3 bounds for 1 hash invalid, with 4 valid; `GetCenter`/`GetRadius`. `Digest(["aa","bb"]) != Digest(["bb","aa"])`, 64 chars. Compare vectors with `vector.Distance(a, b) < 0.001`, not `==`.

- [ ] Python extension → failing test → implementation → pass → commit ("Add batch/manifest payload classes and batch hashing").

---

### Task 4: Cache file

Implements DESIGN.md §10.2, §11 server test 5.

**Files:** `3_Game/DZOMS_Cache.c`, `Test_Cache.c`.

**Interfaces produced:** `static bool DZOMS_Cache.Write(string path, array<ref DZOMS_Batch> batches)` (deletes the file if any write fails); `static bool DZOMS_Cache.Read(string path, array<ref DZOMS_Batch> outBatches)` — clears `outBatches`; returns false and leaves it empty on a missing file, open failure, version mismatch, failed `Read`, count < 0 or > `MAX_BATCHES` (65536), or any batch failing `IsValid()`. No partial use.

**Tests:** round trip of 3 batches (hashes and `index` preserved); changing one position of one loaded batch by 1 cm mismatches exactly that batch (in-memory stand-in for a flipped byte); rejected without script errors: missing file, version + 1, header claiming 3 batches with 2 written, count −5, a structurally invalid batch, random text written with `FPrint`, empty file. If random content causes a script error instead of a clean `false`, report it to the user before working around it (the spec requires rejection without script errors).

- [ ] Failing test → implementation → pass → commit ("Add cache file read/write").

---

### Task 5: Glob matching and type names

Implements DESIGN.md §5.4, §3 (type name), §5.3 (path patterns).

**Files:** `3_Game/DZOMS_Glob.c`, `3_Game/DZOMS_TypeName.c`, `Test_Glob.c`.

**Interfaces produced:**
- `static bool DZOMS_Glob.Match(string pattern, string name)` — both already lower case; exactly the §5.4 algorithm (iterative, one backtrack point, no recursion).
- `DZOMS_TypeName`: `static bool IsModel(string type)` (contains `.p3d`, any case); `static string Normalize(string s)` (lower case, `\` → `/`, leading `/` removed); `static string Of(string type)` (§3 type name in lower case: the class name, or the file name without directory and `.p3d`); `static string ModelPath(string type)` (normalized type for `.p3d` types, else `CfgVehicles <type> model` via `ConfigGetText`, normalized; `""` if absent).

**Tests:** glob — `land_lighthouse` vs `land_lighthouse` ✓, `land_lighthouse2` ✗, `land_lighthous` ✗; `*stair*` vs `land_staircase` ✓, `stair` ✓, `land_stai` ✗; `land_?ouse` vs `land_house` ✓, `land_ouse` ✗; `*` vs `` ✓; `` vs `` ✓; `` vs `a` ✗; `a` vs `` ✗; `a*b*c` vs `axxbyyc` ✓, `axxbyy` ✗; `*a` vs `ba` ✓, `ab` ✗; `**x` vs `x` ✓; `a*` vs `a` ✓; `*ab` vs `aab` ✓ (needs backtracking); `a*ab` vs `aaab` ✓; `dz/structures_bliss/*` vs `dz/structures_bliss/residential/x.p3d` ✓, vs `dz/structures/residential/x.p3d` ✗. Type names — `Of("Land_House_1W10") == "land_house_1w10"`, `Of("dz\\structures\\ruins\\Ancient_Ruins_StairCellar.p3d") == "ancient_ruins_staircellar"`, `Of("plain.p3d") == "plain"`, `Normalize("\\DZ\\Structures\\X.p3d") == "dz/structures/x.p3d"`, `ModelPath` of an unknown class is `""`, `ModelPath("Land_House_1W10")` starts with `dz/` and contains `.p3d`.

- [ ] Failing test → implementation → pass → commit ("Add glob matching and type name helpers").

---

### Task 6: Configuration

Implements DESIGN.md §5.1, §5.2, §5.6, §11 server test 2 (unit level; Task 11 repeats it through the real profile).

**Files:** `3_Game/DZOMS_Config.c`, `Test_Config.c`.

**Interfaces produced:**
- `DZOMS_MapEntry { string file; bool disabled; ref array<string> networked; }`
- `DZOMS_Config` with JSON fields exactly `maps` (`array<ref DZOMS_MapEntry>`), `networked`, `viewDistance`, `proxyDistance`, `batchSize`, `disableCache`, `debug`, plus `[NonSerialized()] bool m_LoadFailed`; constants `PROFILE_DIR = "$profile:dzo-mapstream"`, `DEFAULT_VIEW_DISTANCE = 1500`, `DEFAULT_PROXY_DISTANCE = 1000`, `DEFAULT_BATCH_SIZE = 400`, `MIN_BATCH_SIZE = 50`, `MAX_BATCH_SIZE = 1000`, `MIN_DISTANCE = 100`, `MAX_DISTANCE = 20000`.
- `static DZOMS_Config CreateDefault()` (the §5.1 example with empty `maps`, numbers 0, bools false); `void Normalize()`; `static DZOMS_Config Load(string dir)` — never NULL; creates `dir` and `dir/maps`; missing `dir/config.json` → write `CreateDefault()` with `SaveFile`, then load it like any file; `LoadFile` failure → `Err` log with the loader's message, file untouched, return a normalized empty config with `m_LoadFailed = true`.

**Decisions:**
- Normalization: NULL `maps`/`networked`/`maps[].networked` → empty arrays; NULL map entries removed with a warning; distances: 0 → default, outside 100..20000 (negatives included) → clamped with a warning (**plan choice**, the spec gives no range); `batchSize` 0 → 400, outside 50..1000 → clamped with a warning.
- Map entries with an empty `file`, `..`, separators etc. are handled by the server world (Task 10), not here.
- If a JSON field name (`file`, `debug`, `disabled`) does not compile as a member name, stop and report it: the names are the admin-facing format.

**Tests (each in its own directory under `DZOMS_T.DIR`):** missing file → file written, `maps` dir created, not failed, 0 maps, 8 default rules in order (`@doors` first, `@fixedItems` 4th, `*Stair*` last), effective 1500/1000/400, and the written file still has `viewDistance` 0 (read it back with `FGets > 0`; adjust only the substring to the writer's formatting); broken JSON `{ "maps": [ { "file": "a.dze" } ` → `m_LoadFailed`, 0 maps, defaults set, file byte-identical afterwards; partial file (two maps, the second `disabled` with one rule, `batchSize` 5, `viewDistance` −3, `proxyDistance` 99999) → 2 maps, non-NULL rule arrays, batchSize 50, viewDistance 100, proxyDistance 20000; `"maps": [ null, {...} ]` with `batchSize` 5000 → 1 map, batchSize 1000.

- [ ] Failing test → implementation → pass → commit ("Add configuration loading and normalization").

---

### Task 7: Networked rules (3_Game part)

Implements DESIGN.md §5.3, §6.2 steps 1–3, §11 server test 4 (`!`, per-map rules, `@doors`, `@ladders`).

**Files:** `3_Game/DZOMS_ObjectProbe.c`, `3_Game/DZOMS_NetworkRules.c`, `Test_Rules.c`.

**Interfaces produced:**
- `class DZOMS_ObjectProbe : Managed` with `bool HasOwnActions(Object)` and `bool IsFixedItem(Object)`, both returning false.
- `DZOMS_Rule { string text; bool negated; int token; string pattern; bool matchPath; }`, tokens `TOKEN_NONE = 0, TOKEN_DOORS, TOKEN_LADDERS, TOKEN_ACTIONS, TOKEN_FIXED_ITEMS`.
- `DZOMS_NetworkRules`: `LOCAL = 0, NETWORKED = 1, NEEDS_OBJECT = 2`; `static void Parse(array<string> texts, array<ref DZOMS_Rule> outRules, array<string> outErrors)`; `static DZOMS_NetworkRules Combine(array<ref DZOMS_Rule> globalRules, array<ref DZOMS_Rule> mapRules)`; `int Decide(string type, out string rule)` (may return `NEEDS_OBJECT`); `int DecideWithObject(string type, Object obj, DZOMS_ObjectProbe probe, out string rule)` (obj may be NULL → `@` rules don't match); `static bool HasDoors(Object)`, `static bool HasLadder(Object)`, `static bool IsLadderSelection(string)`, `static void ClearTokenCache()`. `rule` is the text of the deciding rule or `""`.

**Decisions:**
- Parse: trim; leading `!` → negated (trim again); empty → error; `@` tokens case-insensitive (`@doors`, `@ladders`, `@actions`, `@fixeditems`), unknown → error; patterns containing `/` or `\` are path patterns stored `Normalize`d, others lower-cased. Errors are returned; the caller logs them.
- **Evaluate from the last rule to the first**; the first rule that matches decides (= last match wins); no match → `LOCAL`. Reaching an `@` rule without an object → `NEEDS_OBJECT`. This is what lets a later name rule decide without spawning (§6.2 step 1).
- Decision cache per rule list, keyed by type name; never cache `NEEDS_OBJECT`; cache a `DecideWithObject` result only when the object was non-NULL. Compute the model path lazily, only when a path rule is reached.
- `@` results depend only on the type: cache them in a **static** map keyed `"<token>|<type name>"`, shared by all rule lists (stable answers across map files). Tests call `ClearTokenCache()` before and after.
- `@doors`: `Building.Cast(obj)` and `GetDoorCount() > 0`. `@ladders`: any `GetSelectionList` entry that is `ladder` followed by at least one digit and nothing else (case-insensitive).

**Tests:** parse `["@doors", "!@ladders", "Land_X", "!*Y*", "@bogus", "", "!", "  ", "dz\\Structures_Bliss\\*", "@fixedItems", "@ACTIONS"]` → 7 rules, 4 errors, path rule normalized to `dz/structures_bliss/*`, original text kept. `["*Container*", "!Land_Container_1Bo"]`: `Land_Container_1Bo` → LOCAL by `!Land_Container_1Bo` (also when written in upper case), `land_container_2` → NETWORKED by `*Container*`, `Land_House` → LOCAL with `""`. `["!*", "Land_A"]`: A networked, B local by `!*`. `*staircellar` matches the `.p3d` type from Task 5. `Combine(global, map)` vs `Combine(map, global)` flips the container result. `["@doors", "Land_A"]`: A NETWORKED without object, B NEEDS_OBJECT; `["Land_A", "!@doors"]`: A NEEDS_OBJECT. A path rule built from `ModelPath("Land_House_1W10")` with `/` turned into `\` matches the house, not the container. Ladder names: `ladder1` ✓, `Ladder12` ✓, `ladder` ✗, `ladders` ✗, `ladder1a` ✗, `xladder1` ✗. Real objects: a spawned `Land_House_1W10` has doors, `Apple` and NULL don't; `["@doors"]` networks the house, and afterwards `Decide` returns that result from the cache; `["@doors", "!@doors"]` → LOCAL. `@ladders`: find a vanilla type by walking `CfgVehicles` children whose lower-case name starts with `land_` and contains `tower`, `ladder`, `silo` or `tank`, with `scope >= 1`, spawning at most 80 candidates; assert one is found (print it) and that `["@ladders"]` networks it. If none is found, widen the filter; do not weaken the assertion.

- [ ] Failing test → implementation → pass → commit ("Add networked rules with @doors and @ladders").

---

### Task 8: 4_World probe (`@actions`, `@fixedItems`)

Implements DESIGN.md §2 (probe split), §5.3 `@actions`/`@fixedItems`, Review Focus 5.

**Files:** `config.cpp` (add `worldScriptModule`, dependencies `Game, World, Mission`), `4_World/DZOMS_BuildingBase.c`, `4_World/DZOMS_ObjectProbeWorld.c`, `Test_Probe.c`.

**Interfaces produced:** `modded class BuildingBase` with `int DZOMS_CountOwnActions()` and members `m_DZOMS_ActionCount`, `m_DZOMS_CountingActions`; `class DZOMS_ObjectProbeWorld : DZOMS_ObjectProbe`.

**Deviation** from §5.3 ("calls `InitializeActions()` on the probe object"): `DZOMS_CountOwnActions()` sets a counting flag, calls `SetActions()` directly, clears the flag and returns the count. While counting, the overridden `AddAction()` only increments and `RemoveAction()` does nothing (neither may touch the possibly NULL `m_InputActionMap`); otherwise both call `super`. Reason: `InitializeActions()` caches per type (Verified facts).

`IsFixedItem`: `ItemBase.Cast(obj)` and `!CanPutIntoHands(null)`. Never call `CanPutInCargo(NULL)` (not null-safe, §5.3).

**Tests:** own actions: `Land_Misc_Well_Pump_Blue` (twice, separate spawns — the second probe must also be true), `Land_Misc_Well_Pump_Yellow`, `Land_Underground_Panel_Lever` → true; `Land_House_1W10`, `Apple`, NULL → false. Fixed items: `TerritoryFlag` → true; `Apple`, `Rag`, `Land_House_1W10`, NULL → false. Through rules `["@actions", "@fixedItems"]` with the real probe: pump NETWORKED by `@actions`, flag NETWORKED by `@fixedItems`, apple LOCAL.

- [ ] Failing test → implementation → `tools/run-server-tests.sh boot unit` pass (boot proves the World module loads) → commit ("Add 4_World probe for @actions and @fixedItems").

---

### Task 9: Batch builder

Implements DESIGN.md §6.3 steps 1–4.

**Files:** `3_Game/DZOMS_BatchBuilder.c`, `Test_BatchBuilder.c`.

**Interfaces produced:** `DZOMS_ObjectEntry { string type; vector pos; vector ori; float scale; int flags; }` with a constructor taking all five; `static void DZOMS_BatchBuilder.Build(array<ref DZOMS_ObjectEntry> objects, int batchSize, array<ref DZOMS_Batch> outBatches, array<float> outBounds)` — clears both outputs, `outBatches[i].index == i`, `outBounds` = x, y, z, radius per batch.

**Decisions (the spec leaves these open):**
- Cells: key `floor(x/25) floor(y/25) floor(z/25)` as a string; cells kept in an array in first-appearance order plus a key → index map used only for lookups.
- Neighbour search: a cell center is within 100 m of the seed center exactly when the integer offset has `dx² + dy² + dz² ≤ 16` (offset 0 excluded), so look up those ≤ 728 keys instead of scanning all cells. O(cells).
- Candidates sorted by (squared offset, cell order); **stop at the first candidate that does not fit** (literal "while they fit"); it stays unused and seeds later.
- A seed cell larger than `batchSize` emits full batches of `batchSize` objects in object order; the remainder starts the next batch and takes neighbours.
- Objects in a batch: the seed cell's objects, then each added cell's, each in input order. Center = mean of positions, radius = max distance from it.

**Tests (objects 0.1 m apart inside one cell unless noted):** 3 objects at `10 10 10` + 1 at `70 10 10` + 1 at `510 10 10`, size 400 → sizes `4,1`, 8 bounds values, second batch index 1. 120 in one cell, size 50 → `50,50,20`. 40 at x=10 + 20 at x=35, size 50 → `40,20`. 30 at x=10 + 15 `FAR` at x=60 + 15 `NEAR` at x=35 (added after FAR) + 2 `Z` at z=85, size 50 → `45,17`, NEAR in the first batch, FAR not. Two objects at `0 0 0` and `10 0 0` → center x 5, y 0, radius 5. `-1 0 -1` and `-1 300 -1` → `1,1`. Flags, type, scale and orientation are carried. 300 pseudo-random objects (`T<i%7>` at `((i*37)%900, 0, (i*53)%700)`), size 50: building twice gives the same digest, every batch ≤ 50, total 300; reversed input order gives a different digest.

- [ ] Failing test → implementation → pass → commit ("Add deterministic batch builder").

---

### Task 10: Server world: load, spawn, suppress, report, batches

Implements DESIGN.md §6.1, §6.2, §6.3 step 5, §5.5, §11 server tests 4 and 6, Review Focus 1.

**Files:** `3_Game/DZOMS_ServerWorld.c` (with `DZOMS_MapReport`), `DZOMS_TestFixtures.c`, `Test_ServerWorld.c`.

**Interfaces produced:** `DZOMS_ServerWorld(DZOMS_Config config, string mapsDir, DZOMS_ObjectProbe probe)`; `void Load()`; `void Destroy()` (deletes everything it spawned and unsuppresses; for tests); getters `GetConfig()`, `GetObjects()` (`array<ref DZOMS_ObjectEntry>`), `GetSpawned()` (`array<Object>` parallel to the objects, NULL for failed spawns), `GetSuppressed()`, `GetBatches()`, `GetManifest()`, `GetDigest()`; `static Object SpawnOnServer(DZOMS_ObjectEntry e, bool networked)`. Log line `[dzo-mapstream] <n> objects (<x> networked, <y> local) in <b> batches, digest <hex>`.
Test helpers: `DZOMS_TestFixtures.AddObject(EditorSaveData, type, pos, ori, scale, flags)` (sets the `EditorObjectData` fields directly; do not use `EditorObjectData.Create`, it spawns objects), `AddHidden(EditorSaveData, Object)` (via `EditorDeletedObjectData.Create(obj)`), `bool Save(EditorSaveData, path)` (binary: `data.Write(serializer, EditorSaveData.Version)`), `Object FindWorldBuilding()` (a `Land_*` map object with a shape name, no network id, not suppressed, found with `GetObjectsAtPosition` radius 300 around a few town centers at `SurfaceY`).

**Decisions:**
- Order of §6.1 checks per map entry: empty `file` → warning; `disabled` → info; `..`, `/`, `\`, `:` → error; duplicate file name (case-insensitive) → warning; missing → error; `LoadFromFile` NULL → error.
- Object skips: NULL or empty `Type`; `(Flags & EDITOR_ONLY) == EDITOR_ONLY` (never `EditorOnly`, §13); duplicates **across all map files** by `<type name>|<x cm>|<y cm>|<z cm>` (`Math.Round(v * 100)`).
- `Scale <= 0` → 1 (Review Focus 1).
- Spawn per §6.2 with the Task 7 API: `Decide`; if `NEEDS_OBJECT`, spawn local, `DecideWithObject`, and if networked delete and respawn networked; set `flags` from the final decision. A failed spawn (NULL) stays in the data (local unless a name rule networked it) and is counted per type for the report.
- Server spawn flags exactly as §6.2 (local adds `ECE_LOCAL`); then `SetOrientation`, `SetScale` only if ≠ 1, `Update()`; `.p3d` types via `CreateStaticObjectUsingP3D(path, pos, ori, scale, !networked)`.
- Hidden objects: `FindObject()` per entry, NULLs counted as "not found", duplicates across maps dropped via `map<Object, bool>`, one `SuppressMany` at the end; a NULL manager → error log, no crash.
- Report (`DZOMS_MapReport`): always one summary line per map (networked, local, skipped invalid, editor-only, duplicates, hidden found/not found) and one warning per failed type with its count; with `debug`, the §5.5 lines `"<file> networked: <Type> x<n> (<rule>)"` and `"<file> local by rule: <Type> x<n> (<rule>)"` in first-appearance order, plus one line per batch (count, center, radius, hash).
- Batches: `DZOMS_BatchBuilder.Build(objects, batchSize, ...)`, manifest settings copied from the config, `hashes[i] = Hash(batch i)`, digest via `DZOMS_BatchHasher.Digest`.

**Tests** (fixtures under `$profile:dzo-mapstream-tests/maps`, positions at `TEST_ORIGIN + "0 0 400"`, the real probe, global rules `@doors @ladders @actions @fixedItems`):
- `w1.dze` (map rule `*Container*`): `Land_House_1W10`; `Land_Misc_Well_Pump_Blue` with ori `45 0 0`; `TerritoryFlag`; `Apple`; a second `Apple` 4 mm away (duplicate); `Land_Container_1Bo`; `Land_House_1W10` with scale 0 elsewhere; a `.p3d` object (the house model path from `ModelPath`, `/` → `\`); `DZOMS_No_Such_Class`; an `Apple` flagged `EDITOR_ONLY`; an empty type; one hidden world building.
- `w2.dze` (map rule `!Land_Container_1Bo`): another `Land_Container_1Bo`; the w1 house again at the same position (cross-map duplicate); the same hidden building again.
- `w3.dze` listed `disabled`; also listed: `../evil.dze`, `W1.DZE`, `nope.dze`, and an entry with empty `file`.
- Expect: 9 objects; house, pump, flag and w1 container networked; apple, w2 container, `.p3d` object and missing class local; missing class spawned NULL; the scale-0 house has scale 1 in the data and in the world; pump orientation kept; every spawned networked object has a network id that `GetObjectByNetworkId` resolves back to it, every local one has none; the world building is suppressed and collected once; manifest valid, hashes match the batches, all 9 objects in batches, viewDistance 1500, proxyDistance 1000; after `Destroy()` the building is no longer suppressed; a second world from the same config and files has the same digest. The log shows the rejection messages for the bad entries.

- [ ] Failing test → implementation → pass → commit ("Add server world: load maps, spawn, suppress, batch").

---

### Task 11: Requests, rate limits, mission hook, production scenarios

Implements DESIGN.md §6 (`MissionServer.OnInit` after `super`), §6.4, §7 server side, §11 server tests 2 and 3 through the real profile path, Review Focus 4.

**Files:** `3_Game/DZOMS_RateLimiter.c`, `3_Game/DZOMS_ServerWorld.c` (requests), `3_Game/DZOMS_DayZGame.c`, `5_Mission/DZOMS_MissionServer.c`, `Test_Requests.c`, `Test_Scenarios.c`, `DZOMS_TestMission.c`, `tools/run-server-tests.sh`.

**Interfaces produced:**
- `DZOMS_RateLimiter`: `bool AllowManifest(string id, int nowMs)`, `bool AllowBatch(string id, int nowMs)`, `void Remove(string id)`, `void Sweep(int nowMs)` (drops entries not seen for > 120 s), `int Count()`. The token bucket starts full (20), refills 0.02 tokens/ms, capped at 20. Collect keys first, then remove (never remove while iterating the map).
- `DZOMS_ServerWorld`: `ANSWER_NONE = 0`, `ANSWER_MANIFEST = 1`, `ANSWER_BATCH = 2`; `int HandleRequest(string senderId, int rpcType, int value, int nowMs)` — validation and limits only, no network, so tests call it directly: manifest → rate limit, then protocol check (mismatch → warning naming both versions, no answer); batch → index range check, then rate limit; any other id → none. `void OnRpc(PlayerIdentity sender, int rpcType, ParamsReadContext ctx)` — ignore a NULL sender, read one int, `HandleRequest(sender.GetId(), ...)`, send the manifest or `m_Batches[value]` with `ScriptRPC`, target NULL, guaranteed, to the sender. `void OnPlayerDisconnected(string uid)`, `void SweepRateLimits(int nowMs)`, `DZOMS_RateLimiter GetRateLimiter()`.
- `modded class DayZGame`: `ref` member for the server world, `DZOMS_SetServerWorld/DZOMS_GetServerWorld`; `OnRPC`: if `target` is set or the id is outside `FIRST..LAST`, call `super` and return; request ids go to the server world if present; our ids never reach `super`. (Task 12 adds the client half.)
- `modded class MissionServer`: `OnInit` → `super`, `DZOMS_Config.Load(PROFILE_DIR)`, create a `DZOMS_ObjectProbeWorld` in a local, create the world, register it in `DayZGame` **before** `Load()`, log `ready: digest <hex>`. `OnUpdate` → every 60 s `SweepRateLimits(g_Game.GetTime())`. `PlayerDisconnected(player, identity, uid)` → `OnPlayerDisconnected(uid)`, then `super`.

**Deviation** from §6.4: entries are removed in `PlayerDisconnected` (always has the uid) instead of `InvokeOnDisconnect` (skipped when the player entity is gone, no uid), plus the idle sweep (Review Focus 4).

**Test addon changes:** the test `MissionServer.OnInit` now calls `Test_Scenarios.Prepare(scenario)` before `super.OnInit()` and `Test_Scenarios.Check(scenario)` after it. `Prepare("fixtures")` writes `fixture_a.dze` (house, pump, flag, container, 60 apples in a 10 × 6 grid of 10 m) and `fixture_b.dze` (a container 20 m from a's, 20 apples 400 m away) into `$profile:dzo-mapstream/maps` at `TEST_ORIGIN + "0 0 800"`. `Check`: `unit` → config loaded, 0 objects, empty manifest; `noconfig` → config file exists, not failed, 8 rules, viewDistance 1500; `broken` → `m_LoadFailed`, 0 objects; `fixtures` → 85 objects, ≥ 3 batches, every object spawned, a network id exactly for the networked ones (resolving back), 4 networked.

**Script scenarios:** `noconfig` (delete `profiles/dzo-mapstream` first; afterwards `config.json` must exist); `broken` (write `{ "maps": [ { "file": "a.dze" } `, keep a copy; afterwards `cmp` must succeed and the log must contain `[dzo-mapstream] ERROR: config error`); `fixtures` (config: `fixture_a.dze`, `fixture_b.dze` with map rule `!Land_Container_1Bo`, global rules `@doors @ladders @actions @fixedItems *Container*`, `batchSize` 50, `debug` true); `determinism` (fixtures config twice → identical `ready: digest`, then the maps in reverse order → a different digest; all three runs use scenario name `fixtures` and must pass the test checks).

**Tests (`Test_Requests`):** limiter — manifest at 0 ✓, 500 ✗, 1000 ✓, other id at 500 ✓; 25 batch requests at 0 → 20 allowed; at 50 ms one more ✓, then ✗; at 60 s 25 requests → 20 (cap); 3 ids tracked; `Remove` → 2; `Sweep(180001)` → 0. Requests on a world from a 2-object fixture (objects 500 m apart → 2 batches): manifest answered, rate limited at 500 ms, wrong protocol at 2 s → none; batch −1 and `batchCount` → none, 1 → batch; a server→client id → none; 30 batch requests from a new id at once → 20 answered; `OnPlayerDisconnected` of that id → limiter count 1.

- [ ] Failing tests → implementation → `tools/check-style.sh && tools/run-server-tests.sh` (all scenarios) pass → commit ("Answer manifest and batch requests with per-identity rate limits").

---

### Task 12: Client world, part 1: manifest, download, cache

Implements DESIGN.md §8, §7 client side, §4 client cache path, §10.2 cache use, Review Focus 3.

**Files:** `3_Game/DZOMS_ClientWorld.c` (with `DZOMS_ClientBatch`), `3_Game/DZOMS_DayZGame.c`, `Test_ClientDownload.c`.

**Interfaces produced:**
- `DZOMS_ClientBatch`: states `NOT_RECEIVED = 0, UNLOADED = 1, LOADING = 2, LOADED = 3, UNLOADING = 4`; fields `ref DZOMS_Batch data; int state; vector center; float radius; float distance; ref array<Object> objects; ref array<Object> proxies; ref array<int> networked; int cursor; int unloadCursor;`; `void SetData(DZOMS_Batch)` (state UNLOADED, one NULL slot per entry in `objects` and `proxies`, `networked` = indices of networked entries).
- `DZOMS_ClientWorld(string cachePath)` (`""` = resolve when the manifest arrives); `static string CachePathForHost()` (`$profile:dzo-mapstream/cache/<address>_<port>.bin`, `:` → `-`, `""` if `GetHostAddress` fails or returns an empty address); `void Update(vector camera, int nowMs, bool inGame)`; `void OnRpc(int rpcType, ParamsReadContext ctx)` (reads into a new instance, then calls the handler with `g_Game.GetTime()`); `void HandleManifest(DZOMS_Manifest m, int nowMs)`; `void HandleBatch(DZOMS_Batch b, int nowMs)`; test seam: public `bool m_Offline` — when true, requests are appended to public `m_SentTypes`/`m_SentValues` instead of being sent (only then, so production memory stays bounded); getters `GetManifest()`, `GetBatchCount()`, `GetClientBatch(i)`, `GetReceivedCount()`, `GetDownloadedCount()`, `GetFromCacheCount()`, `IsComplete()`, `GetInFlight()`.
- `DayZGame`: client world `ref` member, `DZOMS_SetClientWorld/DZOMS_GetClientWorld`; `MANIFEST`/`BATCH` (target NULL) go to the client world if present.

**Decisions (beyond §8):**
- A constructor with a path loads the cache immediately (`DZOMS_Cache.Read`, then `Hash` each batch into `map<string, ref DZOMS_Batch>`); log the count.
- Manifest: ignore it if one is already stored, if `!IsValid()`, or if its protocol differs (keep requesting). Create one `DZOMS_ClientBatch` per entry from the bounds. If the cache was not loaded yet, resolve the path now and load it. `disableCache` → delete an existing cache file and use nothing. Otherwise take the cached batches whose hash is in the manifest (in manifest order — spawn order is decided later by distance, so "closest first" does not matter here), set their `index`, remove them from the map. Then clear the map and start downloading.
- After 10 unanswered manifest requests, log once that the server may not run this mod version (§7 "logs why").
- Batch: ignore without a manifest, with the index out of range, or when already received. `IsValid()` first, then the hash against the manifest; on failure log and, if it is the in-flight index, request it again at once. A valid late batch for an index not in flight is accepted.
- Download loop exactly as §8.4 with distance `max(0, distance(camera, center) - radius)` from the last `Update` camera. Completion: log the totals; write the cache in manifest order unless `disableCache`, nothing was downloaded, or there is no path; create `$profile:dzo-mapstream` and `.../cache` first.

**Tests** (3 server batches of 5 apples at x = 0, 1000, 2000; manifest view 1500, proxy 1000; cache at `DZOMS_T.DIR + "/client_cache.bin"`, deleted first; camera `2000 10 0`; worlds with `m_Offline = true`):
- nothing sent before IN_GAME; manifest request at 100; none at 1000; second at 3100; an invalid manifest (one bound removed) is ignored and a third request goes out at 6200;
- valid manifest at 7000 → `REQUEST_BATCH 2` (closest), in flight 2; a second manifest is ignored;
- batch 2 with a position moved 1 cm → rejected and re-requested; batch 2 with `typeIndex` out of range → rejected without a script error; the correct batch 2 → received, state UNLOADED, next request 1; index 7 and a duplicate of 2 are ignored without sends;
- timeouts from t0 = 7200: nothing at t0+4999, resend at t0+5000, again at +10000, +15000, +20000; at +25000 (5th) no resend; nothing at +40000; request 1 again at +55000;
- batches 1 and 0 → complete, 3 downloaded, cache file exists;
- a new world on the same path: manifest → 3 from the cache, complete, only the manifest was requested;
- a manifest where batch 0 changed (position +0.5 m) → 2 from the cache, batch 0 requested;
- a `disableCache` manifest → cache file deleted, nothing used; after all batches, still no file;
- an empty manifest → complete at once, no file;
- `CachePathForHost()` on the dedicated server returns `""` (if it returns an address there, change the expectation and note the value in the commit message; the spec marks this unverified).

- [ ] Failing test → implementation → pass → commit ("Add client download with hash checks, retries and cache").

---

### Task 13: Client world, part 2: batch states, spawning, unloading, proxies

Implements DESIGN.md §9, §8 (camera position everywhere), §1 goal 6 (bounded work per frame).

**Files:** `3_Game/DZOMS_ClientWorld.c`, `Test_ClientSpawn.c`.

**Interfaces produced:** `static Object DZOMS_ClientWorld.SpawnLocal(string type, vector pos, vector ori, float scale)` (§9.4 flags; `.p3d` via `CreateStaticObjectUsingP3D(..., true)`), `bool IsFailedType(string type)`, `void Destroy()` (deletes all objects and proxies; received batches back to UNLOADED), `static bool NetworkedInstancePresent(string type, vector pos, Object exclude)` (§9.6: `GetObjectsAtPosition3D(pos, 0.5)`, not `exclude`, non-zero network id, within 5 cm, and `DZOMS_TypeName.Of(GetType())` or `Of(GetShapeName())` equals `Of(type)`).

**Decisions (beyond §9):**
- `Update` after the download step: distances every 250 ms, then the spawn step, the unload step, then the proxy pass (a new pass starts every 1 s; a pass may span frames).
- State switches exactly as §9.2. Entering LOADING resets the spawn cursor to 0 (entries that still exist are skipped); entering UNLOADING sets the unload cursor to the last entry.
- The spawn budget (100) counts spawn attempts; skipped entries (networked, already spawned, known-failed type) don't count. Pick the LOADING batch with the smallest distance, repeat while budget is left. A batch becomes LOADED when its cursor reaches the end. A NULL spawn marks the type failed (one warning per type, never retried).
- The delete budget (200) counts deleted objects and proxies. Pick the UNLOADING batch with the largest distance; walk its cursor down; UNLOADED when it passes 0.
- Proxy pass: walk only the `networked` index lists of LOADED batches, ≤ 50 entries per frame. Without a proxy: spawn one if the distance is > `proxyDistance + 50`, the type has not failed and no networked instance is present. With a proxy: delete it if the distance is < `proxyDistance - 50` or a networked instance (other than the proxy) is present.

**Tests** (base `TEST_ORIGIN + "0 0 -800"`; batch 0 = 250 apples (5 rows of 50, 0.1 m apart) + one networked `Land_House_1W10` at `base + "10 0 10"` (entry 250) + 3 × `DZOMS_No_Such_Class`; batch 1 = 10 apples 3000 m away; manifest view 1500, proxy 1000, `disableCache`; world with `""` path and `m_Offline`; time advanced explicitly, ≥ 250 ms between distance-relevant steps, 16 ms between frames):
- first frame at the base: batch 0 LOADING with 100 live objects, batch 1 UNLOADED; second frame 200; third 250 and LOADED; house slot NULL; the missing class remembered; batch 1 empty;
- camera `base + "1200 0 0"` after 1 s: batch 0 still LOADED, a proxy exists for the house and has no network id;
- create a networked house at the same position (`CreateObjectEx` without `ECE_LOCAL`, with `ECE_NOPERSISTENCY_WORLD`): `NetworkedInstancePresent` true; after the next pass the proxy is gone; delete the networked house: the proxy comes back; camera back at the base: proxy gone;
- camera `+1600`: still LOADED (hysteresis); `+1800`: UNLOADING with 50 live (200 deletes per frame); back at the base: LOADING, after a few frames LOADED with 250 live, and the 50 survivors are still in the array (no duplicates);
- `+1800` for two frames: UNLOADED, 0 live; back at the base: loading again; `Destroy()` → 0 live.
- The ~7 m batch radius makes these offsets work; if the builder's center differs, adjust only the offsets, keeping: proxy appears > 1050 m, disappears < 950 m, batch kept ≤ 1700 m, unloaded > 1700 m.

- [ ] Failing test → implementation → pass → commit ("Add client batch states, budgeted spawning and distant proxies").

---

### Task 14: Client mission hook, README, in-game test list

Implements DESIGN.md §8 intro (lifecycle), §11 server test 7 and the client tests (manual), Review Focus 2.

**Files:** `5_Mission/DZOMS_MissionGameplay.c`, `README.md`, `Test_Smoke.c` (one addition).

**Decisions:**
- `MissionGameplay.OnInit`: after `super`, unless `g_Game.IsDedicatedServer()`, create `DZOMS_ClientWorld(DZOMS_ClientWorld.CachePathForHost())` in a local and register it in `DayZGame`. `OnUpdate`: after `super`, `Update(g_Game.GetCurrentCameraPosition(), g_Game.GetTime(), <game state is IN_GAME>)` (compute the bool into a local first). `OnMissionFinish`: `Destroy()` and unregister, then `super`.
- **§11 server test 7** ("client code compiles"): this plan uses no `#ifdef SERVER` blocks — all client classes and `MissionGameplay` are compiled on the dedicated server too, so every `boot`/`unit` run already proves they compile. Add a smoke check that `DZOMS_ClientWorld` can be constructed on the server with `""`. If `SERVER` ifdefs are ever added, the swapped-define build the spec describes becomes necessary.
- `README.md` for admins: installation (`-mod=` on server and clients, Dabs Framework required), the profile layout (§4), the config fields and rule syntax (§5), the networked report, where clients keep their cache and how to clear it, and the in-game test list below.

**In-game test list (manual; put it in the README and in the final hand-off):**
1. First join downloads closest-first (order of batch requests in the client log), the area around the player is complete first; the cache file appears under `$profile:dzo-mapstream/cache/`.
2. Second join: the log shows all batches taken from the cache, no download.
3. Doors open, ladders can be climbed, levers and wells work, shooting range targets (workshop 3420700027, 3034202210) react to hits and can be reset; decoration items cannot be taken.
4. Walking away and back: batches unload and load without hitches; no duplicated objects; proxies of networked buildings appear and disappear at `proxyDistance` without gaps or doubles (confirms the 1000 m network range assumption, §9.6).
5. Hidden world objects are hidden.
6. Once with `batchSize: 1000` and a map with long `.p3d` paths: every batch arrives (Review Focus 2).
7. `*Stair*` and `*Switch*` rules: decide with the user whether they stay (§5.3 leaves it to this test).

- [ ] Smoke addition → implementation → `tools/check-style.sh && tools/run-server-tests.sh` (all scenarios) pass → commit ("Add client mission hook and README").
- [ ] Final step: report what is verified headless and that the in-game list is still open; do not claim the client side works before it was tested in game.

---

## Self-review notes

- Spec coverage: §1 goals → Tasks 9–13; §2 → 1, 8; §3 → 5; §4 → 6, 12; §5.1–5.6 → 6, 7, 8, 10; §6.1–6.3 → 9, 10; §6.4 → 11; §7 → 3, 11, 12; §8 → 12, 14; §9 → 13; §10 → 2, 3, 4; §11 → harness + every task, client part in Task 14; §12 out of scope; §13 → Global Constraints; §14 → File Structure (adds the helpers `DZOMS_Log`, `DZOMS_TypeName`, `DZOMS_RateLimiter`, `DZOMS_MapReport`, `DZOMS_ClientBatch`, `DZOMS_ObjectEntry`).
- Deviations: Task 8 (probe calls `SetActions()` in counting mode), Task 11 (`PlayerDisconnected` + idle sweep), Task 10 (scale ≤ 0 → 1), Task 6 (distance range 100..20000 is a plan choice), Task 14 (no `SERVER` ifdefs, so spec test 7 is covered by every boot).
