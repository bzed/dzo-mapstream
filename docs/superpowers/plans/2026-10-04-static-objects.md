# Config versioning and static objects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Load the `dayz-dev` skill before writing any Enforce Script: it has the no-hallucination rules, the API lookup (`api.json`) and the local server test procedure this plan relies on.

**Goal:** Version `config.json` with step-by-step migration, then replace local class objects by static models (so players can climb map objects), after an in-game check that static models fix climbing.

**Architecture:** Three phases, each ending in a state that can be shipped or abandoned. Phase 1 adds `configVersion` and the migration machinery to `DZOMS_Config` with `CURRENT_VERSION = 1` (no migration step yet), so it ships on its own. Phase 2 is a throwaway prototype on its own branch that turns every non-networked class object into its static model, packaged for an in-game check by the user; **stop there and wait for the user's verdict**. Phase 3, only after a positive verdict, implements DESIGN.md section 13 properly (`@retextured`, `spawnMode`, migration 1 -> 2, report, docs).

**Tech Stack:** Enforce Script (DayZ 1.29 and 1.30), Dabs Framework (`DF_Scripts`), the repo's headless test addon and `tools/run-server-tests.sh`, packaging with the dayz-dev skill's `dayz-mod-pack.sh` (bzed's armake2 fork).

**Spec:** `DESIGN.md` section 13 (13.5 for config versioning), plus sections 5 and 6.2 it extends. Read 13 completely before Task 1. This plan does not contain the implementation: it fixes order, interfaces, verified facts, decisions the spec leaves open, and the tests. Where it deviates from the spec it says **Deviation** and why.

## Global Constraints

- Target DayZ 1.29 and 1.30: every phase runs `tools/run-server-tests.sh` on both (`DZOMS_SERVER=exp` for 1.30). Use `g_Game`, never `FindFile`.
- Prefixes: classes `DZOMS_`, modded-class members `m_DZOMS_`, log lines `[dzo-mapstream]` (through `DZOMS_Log`).
- Unix line endings, no trailing whitespace, tabs in `.c` files; `tools/check-style.sh` passes before every commit.
- Config zero value = default (DESIGN 5.2). `configVersion` 0 / missing = version 1.
- Never write a config file that failed to parse; never write a file of a newer version; write a migrated file only after a successful backup.
- Never edit or write into the Steam directories; tests run in server trees from `make-server-tree.sh` on ports from `free-ports.sh`.
- Uploading to the Workshop is the user's call: print the command with `dayz-mod-pack.sh publish-hint`, never run it.
- Enforce pitfalls: DESIGN section 14, and the list in `docs/superpowers/plans/2026-10-01-dzo-mapstream.md` (no ternary, no duplicate local names per function, `ref` only on members, no `new` assigned to parameters, test seams must not need constructors with parameters).

## Verified facts this plan relies on

- `proto native bool CopyFile(string sourceName, string destName)` and `proto native bool DeleteFile(string name)` exist (`1_Core/DayZ/proto/EnSystem.c:528,531`). Whether `CopyFile` accepts `$profile:` paths and overwrites an existing target is **not verified**: Task 2 tests it on 1.29 and 1.30; the design deletes the target first anyway.
- `JsonFileLoader<DZOMS_Config>.SaveFile(path, c, error)` is already used for the default file (`DZOMS_Config.Load`).
- `.dze` files store model objects as `dz\structures\walls\wall_indcnch_10.p3d`: lower case, backslashes, no leading backslash (from `strings` over the sample maps). `CfgVehicles <type> model` values usually start with `\` and are mixed case. `DZOMS_TypeName.ModelPath()` lower-cases and converts to `/`; it is for matching, not for spawning.
- Vanilla `ObjectSpawner` creates `.p3d` objects with `CreateStaticObjectUsingP3D(path, pos, ypr, scale, true)` on the server only and calls `g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate()` afterwards (`3_Game/DayZ/ObjectSpawner.c`).
- `CreateStaticObjectUsingP3D(string p3dFilename, vector position, vector orientation, float scale = 1.0, bool createLocal = false, bool objSpawner = false)` (`3_Game/DayZ/Global/Game.c:680`).
- Test harness: suites are classes with a static `Run()` registered in `DZOMS_TestSuites.RunAll()` (`tests/.../DZOMS_TestMission.c`); checks via `DZOMS_T.Check/Eq/EqInt/EqFloat`; per-case config dirs via `DZOMS_Test_Config.CaseDir(name)`; scenarios that need files on disk before boot live in `scenario_prepare()` of `tools/run-server-tests.sh`.

## Review Focus

1. **A config file without `networked`** (NULL list after `LoadFile`): migration runs before `Normalize()`, so every step must cope with NULL lists. Migration 1 -> 2 creates the list with `@retextured` as its only rule. Pinned in Task 2 (NULL list survives a test step) and Task 7.
2. **Admin edits the file while the server runs, or the profile directory is read-only**: backup or write fails. The server must still start with the migrated config in memory and must not leave a half-written file. Pinned in Task 2 (backup failure path through a test seam).
3. **The default file written on first start is reloaded in the same start** (DESIGN 5.6): it must carry `configVersion = CURRENT_VERSION` and therefore never trigger a migration or a backup. Pinned in Task 1.
4. **Models whose class has no `model` or whose model cannot be created** (content mod present on clients only, proxy-only classes): the prototype must log and fall back, not spawn nothing silently. Task 3 logs them; Task 6 turns them into networked objects with `no model`.
5. **Two copies of a static object on the client** (server's `createLocal` copy leaking through the network): headless tests cannot see this. It is an explicit item of the user's check in Task 4 and of the final in-game list.

---

## Phase 1: config versioning

### Task 1: `configVersion` field, default file, newer-version guard

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Config.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Config.c`

**Interfaces:**
- Produces: `int configVersion;` (serialized member of `DZOMS_Config`); `static const int CURRENT_VERSION = 1;`; `static int EffectiveVersion(int fileValue)` (0 -> 1, else the value); `[NonSerialized()] bool m_NewerVersion`, set when the file is newer than `CURRENT_VERSION`.

- [ ] **Step 1: Write the failing tests** in `Test_Config.Run()`:
  - missing file: written file contains `"configVersion": 1`, loaded `configVersion` is 1;
  - existing file without `configVersion` (write `{ "maps": [] }` with `WriteText`): loads, `EffectiveVersion` is 1, file text unchanged (compare `ReadText` before/after), no `.v1.bak` next to it;
  - file with `"configVersion": 7`: loads with its other fields, `m_NewerVersion` true, file text unchanged;
  - `EffectiveVersion(0) == 1`, `EffectiveVersion(1) == 1`, `EffectiveVersion(3) == 3`.
- [ ] **Step 2: Run** `tools/run-server-tests.sh unit` — expect the new checks to fail (compile errors count as failure; read `script_*.log`).
- [ ] **Step 3: Implement:** add the field (zero default), set it in `CreateDefault()`, in `Load()` after a successful `LoadFile` compare `EffectiveVersion(configVersion)` with `CURRENT_VERSION`; newer: `DZOMS_Log.Err` once ("config.json has version N, this mod knows up to M: update the mod; the file is used as it is and not changed"), set the flag, continue to `Normalize()`.
- [ ] **Step 4: Run** `tools/run-server-tests.sh unit noconfig broken` on 1.29, then `DZOMS_SERVER=exp` on 1.30 — all pass; `broken` still proves the broken file is untouched.
- [ ] **Step 5: Commit** (`Config: configVersion field, newer files are used unchanged`).

### Task 2: migration machinery with backup and write-back

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Config.c`
- Create: `dzo_mapstream/scripts/3_Game/DZOMS_ConfigMigration.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Config.c`
- Modify: `DESIGN.md` (5.2 field table, 5.6, move the versioning part of 13.5 into section 5), `README.md` (config table: `configVersion`, "written by the mod, do not change it"; a short "Updates" note about backups)

**Interfaces:**
- Consumes: Task 1 (`configVersion`, `CURRENT_VERSION`, `EffectiveVersion`).
- Produces:
  - `class DZOMS_ConfigMigration` (no constructor parameters) with overridable `int CurrentVersion()` (returns `DZOMS_Config.CURRENT_VERSION`), `bool Step(DZOMS_Config c, int toVersion, out string what)` (applies the step to `toVersion`, returns false for an unknown step; `what` is the log text, e.g. "added @retextured"), and `bool Backup(string path, string backupPath)` (delete target, `CopyFile`; overridable so tests can make it fail).
  - `DZOMS_Config.Load(string dir)` keeps its signature; a second overload `Load(string dir, DZOMS_ConfigMigration migration)` is the test seam, the first one passes `new DZOMS_ConfigMigration()`.
  - `[NonSerialized()] int m_MigratedFrom` (0 = no migration) and `[NonSerialized()] bool m_MigrationWritten` for tests and the log.

- [ ] **Step 1: Write the failing tests** with a test subclass `DZOMS_TestMigration` (in the test file) whose `CurrentVersion()` returns 2 and whose `Step(c, 2, what)` appends `"test-rule"` to `c.networked` (creating the list if NULL) and reports `"added test-rule"`; a second subclass whose `Backup()` returns false. Cases, each in its own `CaseDir`:
  - version 1 file `{ "networked": ["!Land_X"], "viewDistance": 0 }` -> loaded `networked` = `!Land_X, test-rule`, `configVersion` 2, `m_MigratedFrom` 1, file on disk now has `"configVersion": 2` and `test-rule`, **`viewDistance` still 0 in the file** (written before `Normalize`), loaded `viewDistance` 1500, `config.json.v1.bak` equals the original text;
  - same file loaded again with the same migration: no step, file text unchanged (second start changes nothing);
  - file without `networked`: list created with only `test-rule`;
  - backup fails: loaded config migrated in memory, file text unchanged, `m_MigrationWritten` false;
  - an existing `config.json.v1.bak` is replaced by the new backup;
  - `CurrentVersion()` 3 with no step to 3: error logged, config used at the version reached, file not written.
- [ ] **Step 2: Run** `tools/run-server-tests.sh unit` — expect failures.
- [ ] **Step 3: Implement** DESIGN 13.5 "Loading" steps 1-5 in `Load()`: after `LoadFile` and the newer-version check, loop `v+1 .. CurrentVersion()` calling `Step`; on success set `configVersion`, back up to `<path>.v<old>.bak`, `SaveFile`, log one line per step plus the backup name; then `Normalize()`. The production `Step()` knows no step yet (Phase 3 adds 2).
- [ ] **Step 4: Run** `tools/run-server-tests.sh` (all scenarios) on 1.29 and 1.30, and `tools/check-style.sh`. Note in the commit message whether `CopyFile` handled `$profile:` on both versions.
- [ ] **Step 5: Docs:** DESIGN 5.2 gets `configVersion`, 5.6 gets the migration rules (13.5 keeps only the 1 -> 2 step); README config table and update note.
- [ ] **Step 6: Commit** (`Config: step-by-step migration with backup`). Phase 1 is now releasable on its own; ask the user whether to merge and release it before Phase 2.

---

## Phase 2: prototype, does it fix climbing?

Throwaway work on branch `proto/static-objects` (from the Phase 1 state). Nothing of it is merged; Phase 3 reimplements it properly.

### Task 3: every non-networked class object as its static model

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_TypeName.c` (spawnable model path)
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_ServerWorld.c` (`SpawnEntry` and the load loop)
- Test: a quick check in `Test_ServerWorld.c` (kept for Phase 3)

**Interfaces:**
- Produces: `static string DZOMS_TypeName.SpawnModel(string type)`: `""` for `.p3d` types and classes without `model`; otherwise the config value with a leading `\` removed, lower-cased, backslashes kept, `.p3d` appended if missing (the form of `.dze` model entries). Phase 3 keeps this function.

- [ ] **Step 1: Test** `SpawnModel` on `HMMV_Wreck`, a `Land_Container_1Bo`, a wall used in the fixtures, and a class without a model: non-empty values start with `dz\` and end with `.p3d`; `CreateStaticObjectUsingP3D(SpawnModel(t), pos, ori, 1, true)` returns non-NULL on the headless server for each, and its `GetShapeName()` matches the class instance's. Run on 1.29 and 1.30.
- [ ] **Step 2: Implement** `SpawnModel`; in the server load loop, when the final decision for a class type is local: replace `e.type` by `SpawnModel(e.type)` and spawn static (the existing `.p3d` branch of `SpawnOnServer`); probe objects are deleted first. No model: keep today's local class object and log the type once (`prototype: no model for X`). Call `g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate()` once after all maps are loaded. The client needs no change: it spawns `.p3d` types as static objects already.
- [ ] **Step 3: Run** all scenarios on 1.29 and 1.30. Expected changes only: hashes/digest differ from `main`, the report counts the converted objects under the class name as before. Tests that assert class types in batch entries may fail; do not fix them, list them in the commit message (Phase 3 rewrites them).
- [ ] **Step 4: Commit** on `proto/static-objects` (`PROTOTYPE: spawn local class objects as static models`).

### Task 4: package and hand over for the in-game check

- [ ] **Step 1:** `P=~/.claude/skills/dayz-dev/scripts/dayz-mod-pack.sh; $P -C pack build && $P -C pack check`, then `DZOMS_PACKAGE=1 tools/run-server-tests.sh` on 1.29 and 1.30.
- [ ] **Step 2:** `$P -C pack publish-hint`; give the user the command. Recommend uploading to a separate, unlisted test item instead of the public one (ask which); do not run it.
- [ ] **Step 3: Ask the user to verify** on a dedicated server with a client and their maps (DESIGN 13.2 and 13.10 client list):
  1. climb and vault onto converted objects: `HMMV_Wreck` (hood -> cabin), walls, containers;
  2. every object appears **once** (no second copy on top; look for z-fighting or flicker);
  3. colours of colour-variant classes (containers, signs): which ones lost their texture (input for `@retextured` in Phase 3);
  4. zombies walk around converted objects, not through them;
  5. anything that used to light up, smoke, make sound or break and now does not;
  6. the server log line `prototype: no model for ...`, if any.
- [ ] **Step 4: Stop.** Record the user's answers in DESIGN 13.2 (date, client version, results) and commit that on the design branch. Negative result on 1 or 2: Phase 3 is void; discuss the fallback (networking climbable objects). Positive: delete the prototype branch and continue.

---

## Phase 3: static objects (only after a positive check)

Work on a branch from the Phase 1 state, not from the prototype.

### Task 5: `@retextured` rule

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_NetworkRules.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Rules.c`

**Interfaces:**
- Produces: rule tokens `@retextured` / `!@retextured`, decided from config only (no `NEEDS_OBJECT`); `static bool DZOMS_NetworkRules.IsRetextured(string type)` (cached per type).

- [ ] **Step 1: Test:** a colour variant (pick from the user's Task 4 findings, e.g. a `Land_Container_*` variant) matches; its base class without textures does not; `!@retextured` after it makes it local/static; a `.p3d` type never matches; unknown-token errors still work for other `@` words.
- [ ] **Step 2: Run** unit — fail.
- [ ] **Step 3: Implement** with `g_Game.ConfigGetTextArray("CfgVehicles " + type + " hiddenSelectionsTextures", arr)` and the same for `hiddenSelectionsMaterials`; first verify on the headless server that inherited arrays are returned (test with a variant that sets only textures).
- [ ] **Step 4: Measure** on `tests/dze/*.dze` and the user's maps (`realdze` scenario with `debugLog`): number of types and objects `@retextured` selects. If base classes repeat the model's own textures, compare with the parent class (`ConfigGetBaseName` — look it up in `api.json` first) and record the rule in DESIGN 13.4.
- [ ] **Step 5: Run** all scenarios on 1.29 and 1.30; **commit**.

### Task 6: `spawnMode` and static spawning on the server

**Files:**
- Modify: `DZOMS_Config.c` (`spawnMode` global and in `DZOMS_MapEntry`, normalization), `DZOMS_ServerWorld.c` (decision, report), `DZOMS_TypeName.c` (`SpawnModel` from Task 3, with its test)
- Test: `Test_Config.c`, `Test_ServerWorld.c`, `Test_Scenarios.c`; fixtures in `DZOMS_TestFixtures.c` if a class without model or a colour variant is needed

**Interfaces:**
- Consumes: Task 5 rules, Task 3 `SpawnModel` (reimplemented with its test).
- Produces: `string spawnMode` in `DZOMS_Config` and `DZOMS_MapEntry`; normalized constants `DZOMS_Config.SPAWN_STATIC = "static"`, `SPAWN_EDITOR = "editor"`; `string DZOMS_Config.SpawnModeFor(DZOMS_MapEntry e)`; report counters `static`, `no model`, rule labels `(editor)`, `(no model)`.

- [ ] **Step 1: Tests** from DESIGN 13.10 "Server, headless": `spawnMode` values (missing, `""`, `"static"`, `"STATIC"`, `"editor"`, unknown -> config error, per-map override and `""` inherits); static mode entries carry the model path without the networked flag; networked rule keeps the class with the flag; class without model -> networked, counted `no model`; editor mode networks every class whatever the rules say, `.p3d` objects follow the rules; no probe object left behind; digest stable over two starts and different when `spawnMode` changes (`determinism` scenario variant).
- [ ] **Step 2: Run** — fail.
- [ ] **Step 3: Implement** DESIGN 13.3 and 13.6: decision cache keyed by (type, rule list, spawn mode) including the model path; `ProcessMarkedObjectsForPathgraphUpdate()` once after loading; `SpawnOnServer` no longer creates local class objects except the probe; report wording "static" instead of "local" (13.7).
- [ ] **Step 4: Run** all scenarios on 1.29 and 1.30, check-style; **commit**.

### Task 7: migration 1 -> 2

**Files:**
- Modify: `DZOMS_Config.c` (`CURRENT_VERSION = 2`, default `networked` gets `@retextured` after `@fixedItems`), `DZOMS_ConfigMigration.c` (`Step` to 2)
- Modify: `tools/run-server-tests.sh` (a `migrate` scenario: version 1 file on disk before boot; after the run `cmp` the backup with the original and check the new file with `grep`)
- Test: `Test_Config.c`

- [ ] **Step 1: Tests** (DESIGN 13.10 config version list): version 1 file with custom rules -> `@retextured` first, others unchanged in order, version 2, backup identical to the original, `0` fields still `0`; file already containing `!@retextured` (any case) -> only the version changes; file without `networked` -> list `["@retextured"]`; version 2 file not written; version 3 file used and not written; second start after migration changes nothing; default file has 9 rules with `@retextured` at index 4 (right after `@fixedItems`); update the existing "8 default rules" checks.
- [ ] **Step 2: Run** — fail. **Step 3: Implement.** **Step 4: Run** all scenarios including `migrate` on 1.29 and 1.30. **Step 5: Commit.**

### Task 8: documentation

**Files:** `DESIGN.md`, `README.md`, `workshop.txt`, `DEVELOPMENT.md`

- [ ] **Step 1:** DESIGN: rewrite sections 1 (goal 1 wording), 3 (terms: static object replaces local object), 5 (`spawnMode`, `@retextured`, example config with `configVersion: 2`), 6.2, 9.4, 11 from section 13; remove section 13 except a short "why static objects" paragraph (the climbing finding of 13.1) under 6.2; update section 15 (classes).
- [ ] **Step 2:** README: "Building maps for this mod" from DESIGN 13.8, networked rules table with `@retextured`, config table with `spawnMode` and `configVersion`, update notes (first start migrates `config.json`, backup `config.json.v1.bak`, every player downloads the maps once more).
- [ ] **Step 3:** `workshop.txt` in the same words (Steam BBCode), "static" instead of "local".
- [ ] **Step 4:** DEVELOPMENT in-game list: replace item 3 wording and add the client items of DESIGN 13.10.
- [ ] **Step 5:** `tools/check-style.sh`; **commit**.

### Task 9: release candidate and final in-game check

- [ ] **Step 1:** build, check, `DZOMS_PACKAGE=1` tests on 1.29 and 1.30.
- [ ] **Step 2:** `publish-hint`, hand the command to the user.
- [ ] **Step 3:** ask the user to run the DESIGN 13.10 client list (climb, one copy, colours with and without `!@retextured`, zombies, an `"editor"` map, migration of their real `config.json` with its backup). Record the result in DEVELOPMENT.md.
