# Cache encryption Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Load the `dayz-dev` skill before writing any Enforce Script: it has the no-hallucination rules, the offline API lookup (`scripts/dayz-api.py`) and the local server test procedure this plan relies on.

**Goal:** A client's cache file is useless without joining the server: it is stored ChaCha20-encrypted with a key the server sends in the manifest, and it holds the batches' hash words instead of the batch objects, so loading gets faster too.

**Architecture:** Positions in the hash words change from millimetres to 1/1024 m, so a batch decoded from its words encodes to the same words at any map size. New `DZOMS_ChaCha20` (RFC 8439 block function, XOR over int words) and `DZOMS_CacheKey` (random words, hex parsing, key file). The server resolves an 8-int key at config load and puts it into the manifest (protocol 3). The cache file (version 2) is one encrypted `array<int>`: check word, batch count, then per batch its word count and its 10.1 hash words. The client loads the cache only when the manifest (and so the key) arrives, hashes word ranges directly, decodes only the batches the manifest wants, and keeps every batch's words unchanged to write them back.

**Tech Stack:** Enforce Script (DayZ 1.30), Dabs Framework (`DF_Scripts`), the repo's headless test addon and `tools/run-server-tests.sh`, Python 3 reference scripts under `tests/python/`, packaging with the dayz-dev skill's `dayz-mod-pack.sh`.

**Spec:** `DESIGN.md` section 12 (all of it; read it completely before Task 1), plus 7, 8, 10 it changes. This plan does not contain the implementation: it fixes order, interfaces, verified facts, decisions the spec leaves open, and the tests. Where it deviates from the spec it says **Deviation** and why.

## Global Constraints

- Target DayZ 1.30 only. Run tests with `DZOMS_TREE=/tmp/dzoms-stable tools/run-server-tests.sh [scenario...]` (stable 1.30 server; never a tree under `$HOME`).
- Prefixes: classes `DZOMS_`, log lines through `DZOMS_Log` (`[dzo-mapstream]`). `Print()` cuts a line at 256 characters: keep log lines short.
- Unix line endings, no trailing whitespace, tabs in `.c` files; `tools/check-style.sh` passes before every commit.
- Config zero value = default (DESIGN 5.2): `cacheKey` missing or `""` = generated key. `configVersion` stays 1 (no migration).
- Never call `Math.Randomize(-1)` (reseeds the game's shared generator, DESIGN 12.4).
- Batch words go from the server's batch (or the decrypted file) to the next cache file unchanged (no `Words(Decode(...))` on the client: it is slower, `AppendString` reads strings character by character). Since Task 3 this is no longer needed for correctness.
- Key never stored on the client. Integrity stays with the per-batch SHA-256; encryption adds none and the code must not rely on it.
- Enforce pitfalls: DESIGN section 14, and the list in `docs/superpowers/plans/2026-10-01-dzo-mapstream.md` (no ternary, no duplicate local names per function, `ref` only on members, no `new` assigned to parameters, `out` is reserved, test seams must not need constructors with parameters).
- Never edit or write into the Steam directories. Uploading to the Workshop only when the user says so.

## Verified facts this plan relies on

- `Math.RandomInt(int min, int max)` (max exclusive), `TickCount(int prev)` (microseconds), `GetYearMonthDayUTC(out int, out int, out int)`, `GetHourMinuteSecondUTC(out int, out int, out int)`, `g_Game.GetTime()` (ms), `int.MAX = 2147483647`, `int.AsciiToString()` exist on 1.30 (`dayz-api.py`).
- `string.HexToInt()` exists, but its result for 8 hex digits >= `80000000` is **not verified**: parse the key nibble by nibble (index in `"0123456789abcdef"` of the lower-cased character), which validates at the same time.
- `FileSerializer` writes and reads a whole `array<int>` in one call (prototype, DESIGN 12.2). What a read past the end of the file returns for an `array<int>` is not verified: every reader checks the array's structure afterwards, so an empty or short array is rejected either way.
- Overloads by parameter count work (`DZOMS_Config.Load(dir)` / `Load(dir, migration)`).
- RFC 8439 2.3.2 (key `00..1f`, nonce `000000090000004a00000000`, counter 1), state words as signed ints:
  `-454561520, 358169553, 534581072, -999219037, -940256825, 57196595, -1700126204, 1315755203, 1180992210, 162176775, 98026004, -1576891431, -778300747, -1186064674, -394014517, 1312575650` (checked with a Python reference, equals the hex list in DESIGN 12.7).
- Partial block reference (same key and nonce, counter from 0): data = `(i * 7919) & 0x7fffffff` for `i < 40` (the input `sha_vectors.py` already uses); after `XorWords` the words are `[0] = -40772470`, `[15] = -734733720`, `[16] = -454680608`, `[39] = -392456093`, and `HashWords` of all 40 = `3d0015d399bb5aa79c67df3c881875dbc7a3e6f35f75f397349f79887d781e99`.
- Enforce `float` is 32 bit; `Math.Round(float)` returns a `float` (`dayz-api.py`), assigned to `int`.
- **Deviation from DESIGN 10.1 / 12.3: positions are hashed as `round(x * 1024)`, not `round(x * 1000)`.** With millimetres, a word decoded to float32 and encoded again differs from the original for some positions beyond 8192 m (float32 simulation: server float `8192.021484375` -> word 8192022 -> another word; 755 of 400000 random positions in +-25 km), and Chernarus is 15.4 km wide. With a power of two the round trip is exact for every word the server produces, at any position up to +-2097 km: below 8192 m the word is < 2^23 and converts to float exactly; from 8192 m on `x * 1024` is already an integer with at most 24 significant bits (no rounding at all), and multiplying or dividing by 1024 never rounds. Simulation: 0 of 400000 unstable, largest position error 0.49 mm (was 1.95 mm). Orientations (`* 1000`, values below 8192) and scale (`* 10000`, below 20) already round-trip and stay as they are.
- Protocol is already 2 (proximity messages, DESIGN 13.5). **Deviation:** DESIGN 12.5 says "bump to 2"; it becomes **3**. `CACHE_VERSION` goes from 1 to 2 as written.

## Review Focus

1. **Maps with positions beyond 8192 m** (most of Chernarus): a batch read from the cache and written back must keep its hash on every later join. Pinned in Task 3 (the round trip on the engine, up to 20 km) and Task 7 (three joins in a row, all from the cache).
2. **The server's key changes** (key file deleted, `cacheKey` set or changed): every client cache fails the check word; the client must download everything, log one line, and write a new cache with the new key, without script errors. Pinned in Task 7.
3. **A version 1 cache file from the current release** on every client: rejected without errors and replaced by a version 2 file after the downloads. Pinned in Task 7.
4. **A broken key file** (truncated, wrong word count, all zero): a new key is generated and written, the server starts. Pinned in Task 5.
5. **Manifest key and `disableCache` disagree** (no key with caching on, or a key with caching off): the client ignores such a manifest and keeps requesting; a server never sends one. Pinned in Task 6.

---

### Task 1: `DZOMS_ChaCha20`

**Files:**
- Create: `dzo_mapstream/scripts/3_Game/DZOMS_ChaCha20.c`
- Create: `tests/python/chacha_vectors.py`
- Create: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_ChaCha20.c`
- Modify: `tests/dzo_mapstream_tests/scripts/5_Mission/DZOMS_TestMission.c` (register the suite after `DZOMS_Test_Sha256`)

**Interfaces:**
- Produces: `static void DZOMS_ChaCha20.Block(array<int> key, array<int> nonce, int counter, array<int> dst)` (key 8 ints, nonce 3 ints; `dst` is cleared and gets 16 words); `static void DZOMS_ChaCha20.XorWords(array<int> key, array<int> nonce, array<int> words)` (in place, word `i` XOR keystream word `i`, block counter from 0; 0 words is a no-op).

- [ ] **Step 1: Write `tests/python/chacha_vectors.py`** in the style of `sha_vectors.py`: no argument prints the reference values listed under "Verified facts" (RFC block as signed ints, the 40-word partial case); argument `rounds` prints the Enforce lines of one double round (8 quarter rounds on locals `x0..x15`, rotation as `(x << n) | ((x >> (32 - n)) & ((1 << n) - 1))`). Run it and check its output equals the values above.
- [ ] **Step 2: Write the failing tests** (`DZOMS_Test_ChaCha20.Run()`, suite `chacha20`):
  - `Block` with the RFC key words `50462976, 117835012, 185207048, 252579084, 319951120, 387323156, 454695192, 522067228`, nonce `150994944, 1241513984, 0`, counter 1 equals the 16 signed ints above (one `EqInt` per word);
  - 40-word partial case: the four sampled words and the hash above;
  - `XorWords` twice restores the 40 words; 0 words stays empty; 33 words (three blocks) restore too.
- [ ] **Step 3: Run** `DZOMS_TREE=/tmp/dzoms-stable tools/run-server-tests.sh unit` — expect failure (compile error).
- [ ] **Step 4: Implement** as DESIGN 12.5 says: state in 16 locals, the 10 double rounds with the generated lines pasted in full, constants `1634760805, 857760878, 2036477234, 1797285236`, output = working state + input state. `XorWords` computes one block per 16 words into a reused 16-int array.
- [ ] **Step 5: Run** `... unit` — all pass. Then `tools/check-style.sh`.
- [ ] **Step 6: Commit** (`ChaCha20 over int words (RFC 8439 block function)`).

### Task 2: SHA-256 over a range

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Sha256.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Sha256.c`

**Interfaces:**
- Produces: `static string DZOMS_Sha256.HashWords(array<int> words, int start, int count)`; the existing `HashWords(array<int> words)` becomes `HashWords(words, 0, words.Count())`. The caller guarantees `0 <= start`, `start + count <= words.Count()`.

- [ ] **Step 1: Failing tests:** for n in 1, 13, 14, 16, 17: `Seq(n)` embedded at offset 5 between junk words (`-1` before, `7` after) hashes, with `(arr, 5, n)`, to the same value as `HashWords(Seq(n))` (whose expected values the suite already pins); `count = 0` gives the empty hash.
- [ ] **Step 2: Run** `... unit` — fails.
- [ ] **Step 3: Implement**: the padding loop copies `words[start .. start + count)`; the length words use `count`.
- [ ] **Step 4: Run** `... unit determinism` — all pass, digests unchanged.
- [ ] **Step 5: Commit** (`Sha256: hash a range of a word array`).

### Task 3: positions in 1/1024 m

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_BatchHasher.c`, `DZOMS_Rpc.c` (`PROTOCOL_VERSION = 3` moves here from Task 6: old clients would reject every batch)
- Modify: `tests/python/sha_vectors.py` (`rnd(v * 1024)` for positions)
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Hasher.c`

**Interfaces:**
- Produces: `static int DZOMS_BatchHasher.EncodePosition(float v)` = `Math.Round(v * 1024)` and `static float DZOMS_BatchHasher.DecodePosition(int w)` = `w / 1024.0`, used by `Words()` and (Task 4) `Decode()`; `const int POSITION_SCALE = 1024;`. `Milli()` stays for orientations.

- [ ] **Step 1: Failing tests:**
  - reference words become `... 3 0 1 1024256 51712 2048768 90000 0 0 10000 1 0 1034240 52224 2058240 -45500 1250 0 15000 1 0 1044608 53248 2068480 0 0 0 7500` (type part unchanged); the reference hash is the one the updated `sha_vectors.py` prints;
  - round trip on the engine: for `x` from -20000 to 20000 step 0.1234567 (about 324000 values), plus `8191.999`, `8192.021484375`, `15359.999`, `16383.999`, `16384.001`, `-16384.001`: `EncodePosition(DecodePosition(EncodePosition(x))) == EncodePosition(x)`; count failures, expect 0 (log the first one if any);
  - `|DecodePosition(EncodePosition(x)) - x| <= 0.0005` for the same values;
  - a 1 cm move still changes the hash (existing check keeps passing).
- [ ] **Step 2: Run** `... unit` — fails.
- [ ] **Step 3: Implement**; update the `sha_vectors.py` reference batch and its printed hash.
- [ ] **Step 4: Run** `... unit determinism` — all pass; the digest differs from before, two runs still agree. Grep the tree for other `* 1000` position encodings (there should be none outside `DZOMS_BatchHasher`).
- [ ] **Step 5: Commit** (`Hash positions in 1/1024 m: decode + encode is exact at any map size`).

### Task 4: `DZOMS_BatchHasher.Decode`

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_BatchHasher.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Hasher.c`

**Interfaces:**
- Consumes: `Words()`, `EncodePosition`, `DecodePosition` (Task 3).
- Produces: `static DZOMS_Batch DZOMS_BatchHasher.Decode(array<int> words, int start, int count)`: NULL unless the range is exactly one batch in the 10.1 layout and the result passes `IsValid()`. `index` stays 0.

- [ ] **Step 1: Failing tests:**
  - `Decode(Words(Reference()), 0, n)` gives a batch whose `Words()` equal the input word by word, and whose types equal the original's (a type of 1, 4 and 5 characters covers the zero padding);
  - decoding a range inside a larger array (junk before and after) works;
  - NULL for: `count` one short, one word too many, a type length larger than the words left, type length 0, negative counts, object count not matching the words left, a `typeIndex` out of range;
  - a batch with objects at `8192.021484375`, `15359.999` and `-16384.001` on every axis: `Words(Decode(Words(b)))` equal `Words(b)`.
- [ ] **Step 2: Run** `... unit` — fails.
- [ ] **Step 3: Implement** per DESIGN 12.3: check each count and length against the words left before using it, names with `AsciiToString()` per byte (only `length` bytes), positions with `DecodePosition`, orientations `/ 1000.0`, scale `/ 10000.0`, flags and type index as they are.
- [ ] **Step 4: Run** `... unit` — all pass.
- [ ] **Step 5: Commit** (`BatchHasher: decode a batch from its hash words`).

### Task 5: `DZOMS_CacheKey` and the `cacheKey` setting

**Files:**
- Create: `dzo_mapstream/scripts/3_Game/DZOMS_CacheKey.c`
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Config.c`
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Config.c` (new section `cachekey`, case dirs via `DZOMS_Test_Config.CaseDir`)

**Deviation:** DESIGN 12.6 puts key generation into `DZOMS_Config`. It goes into its own class: the config class stays the JSON model plus normalization, and the client needs the same random words for its nonce.

**Interfaces:**
- Produces:
  - `static array<int> DZOMS_CacheKey.RandomWords(int n)`, `1 <= n <= 16`. Entropy as DESIGN 12.4 (two `Math.RandomInt(0, int.MAX)` per word, `TickCount(0)`, UTC date and time, `g_Game.GetTime()`), mixed by running `DZOMS_ChaCha20.Block` with the 8 random words as key, `{TickCount(0), y*10000+m*100+d, h*10000+min*100+s}` as nonce and `g_Game.GetTime()` as counter; the first `n` output words.
  - `static bool DZOMS_CacheKey.Parse(string hex, array<int> key)`: exactly 64 characters `0-9a-fA-F`, 8 words big endian (`"00010203..."` -> first word `66051`); on false `key` is empty.
  - `static array<int> DZOMS_CacheKey.Resolve(string configured, string dir)` -> 8 ints, never NULL. Valid `configured`: that key, the file is not touched. Invalid: `DZOMS_Log.Err("config error in cacheKey: ...")`, then as if empty. Empty: read `dir + "/cachekey.bin"` (`FileSerializer`, one `array<int>`); missing, unreadable, not 8 words or all zero -> generate, write, log one line.
  - `DZOMS_Config`: serialized `string cacheKey;` and `[NonSerialized()] ref array<int> m_CacheKey`, set by `Load(dir, migration)` after `Normalize()` with `DZOMS_CacheKey.Resolve(cacheKey, dir)`.
  - `static const string DZOMS_CacheKey.FILE = "cachekey.bin";`

- [ ] **Step 1: Failing tests:**
  - `Parse` of `"000102...1f"` gives the RFC key words of Task 1; upper case is accepted; 63 and 65 characters, a `g`, a space -> false, key empty;
  - `RandomWords(8)` twice: 8 words each, not all zero, the two differ;
  - fresh case dir: `Load` writes `cachekey.bin`, `m_CacheKey` has 8 words; a second `Load` of the same dir gives the same words;
  - `cachekey.bin` with 3 words, and with 8 zero words: replaced by a new key, a later `Load` returns that new key;
  - config with a valid `cacheKey`: `m_CacheKey` equals it, no `cachekey.bin` is created;
  - config with `"cacheKey": "xyz"`: `m_CacheKey` has 8 words (generated or from the file), `m_LoadFailed` stays false.
- [ ] **Step 2: Run** `... unit` — fails.
- [ ] **Step 3: Implement** the class and the config hook.
- [ ] **Step 4: Run** `... unit noconfig broken` — all pass; `noconfig` still writes the default config, `broken` still leaves the broken file untouched.
- [ ] **Step 5: Commit** (`Cache key: generated once per server or set in config.json`).

### Task 6: key in the manifest

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Batch.c` (`DZOMS_Manifest`), `DZOMS_ServerWorld.c` (where the manifest is filled, near `m_Manifest.disableCache = ...`)
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_ServerWorld.c`, `Test_ClientDownload.c` (`MakeManifest` sets a key)

**Interfaces:**
- Consumes: `DZOMS_Config.m_CacheKey` (Task 5).
- Produces: `ref array<int> cacheKey` in `DZOMS_Manifest` (created in its constructor); `IsValid()` additionally requires `cacheKey` non-NULL and `cacheKey.Count() == 8` when `!disableCache`, `== 0` when `disableCache`. (`PROTOCOL_VERSION` is 3 since Task 3.) The server copies `m_Config.m_CacheKey` into the manifest unless `disableCache`. A config built in a test without `Load` has no `m_CacheKey`: the server then uses `DZOMS_CacheKey.RandomWords(8)` (log nothing).
- `DZOMS_Test_ClientDownload.MakeManifest(batches, bounds, disableCache)` keeps its signature and sets a fixed test key (8 words) unless `disableCache`; add `MakeManifestKey(batches, bounds, array<int> key)` for Task 6.

- [ ] **Step 1: Failing tests:** server world from the fixtures: manifest key has 8 words and equals the config's; with `disableCache` it is empty. Client: a manifest with caching on and 0 key words, and one with `disableCache` and 8 words, are both ignored (`GetManifest() == null`), the next manifest request still goes out. `Last(w, RM, 3, ...)` for the protocol.
- [ ] **Step 2: Run** `... unit` — fails.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `... unit fixtures determinism realdze` — all pass.
- [ ] **Step 5: Commit** (`Manifest carries the cache key`).

### Task 7: cache version 2 and the client

**Files:**
- Modify: `dzo_mapstream/scripts/3_Game/DZOMS_Cache.c` (rewrite), `DZOMS_Rpc.c` (`CACHE_VERSION = 2`), `DZOMS_ClientWorld.c` (`DZOMS_ClientBatch`, constructor, `LoadCache`, `HandleManifest`, `HandleBatch`, `Complete`)
- Test: `tests/dzo_mapstream_tests/scripts/5_Mission/Test_Cache.c` (rewrite), `Test_ClientDownload.c`

**Interfaces:**
- Consumes: Tasks 1-6.
- Produces:
  - `const int DZOMS_Cache.CHECK_WORD = 1146768707;` (`0x445A4D43`), `MAX_BATCHES = 65536` stays.
  - `static bool DZOMS_Cache.Write(string path, array<int> key, array<int> nonce, array<ref array<int>> batchWords)`: version, the 3 nonce ints (three `Write(int)`), one encrypted `array<int>` (check word, batch count, per batch count + words). Deletes the file if any write fails.
  - `static bool DZOMS_Cache.Read(string path, array<int> key, array<int> words, array<int> starts, array<int> counts)`: clears the three arrays; on success `words` is the decrypted plaintext and `starts[i]`/`counts[i]` locate batch `i` in it. False (arrays empty, no partial use) on: missing file, version != 2, a failed read, wrong check word, batch count outside `0..MAX_BATCHES`, a negative word count or one past the end, words left over.
  - `DZOMS_ClientBatch`: `ref array<int> words` and `void SetData(DZOMS_Batch b, array<int> batchWords)` (replaces `SetData(b)`).
  - `DZOMS_ClientWorld(string cachePath)` no longer reads the cache. `HandleManifest` reads it when `!disableCache`, with `m.cacheKey`; for each manifest hash, a cached range with that `HashWords(words, start, count)` is decoded (`Decode`), its words copied into a new array, `SetData`. Each cached range is used at most once (as today). A failed read logs one line naming the reason class (missing, other version, other key, broken); a different key is not an error.
  - `HandleBatch` keeps the words it hashes (`Words(b)` then `HashWords`), `Complete()` writes each batch's `words` with `m.cacheKey` and `DZOMS_CacheKey.RandomWords(3)` as nonce.

- [ ] **Step 1: Failing cache tests** (`Test_Cache`): helper `Words3()` = the `Words()` of today's `Three()` batches; helper `WriteRaw(path, version, nonce, plaintext, key)` encrypts with `XorWords` and writes like `Write` does, so tests can build any plaintext.
  - round trip: `Read` gives 3 ranges whose words equal the written ones word by word;
  - wrong key: false, arrays empty;
  - one flipped word inside batch 1's object data (`WriteRaw` with a modified plaintext): `Read` succeeds, `HashWords` of range 1 differs, ranges 0 and 2 match;
  - rejected without script errors: missing file, version 1 file written the old way (`ser.Write(1); ser.Write(3);` then three `DZOMS_Batch`), header only, claimed batch count 4 with 3 batches, word count past the end, a word left over, negative batch count, 50 random ints, an empty file.
- [ ] **Step 2: Failing client tests** (`Test_ClientDownload`, cache part):
  - existing flow with the key: download writes the cache, second world (constructor + manifest) takes all 3 from the cache, changed batch 0 -> 2 from the cache; `w3` then receives batch 0, completes, and a fifth world takes all 3 from the cache (mixed write);
  - manifest with a different key (`MakeManifestKey`): 0 from the cache, everything requested, after completion the file is rewritten and a world with that key takes all 3 from the cache;
  - a version 1 file at the cache path: 0 from the cache, no script error, version 2 file after completion;
  - beyond 8192 m: server batches with one object at `x = 8192.021484375` and one at `15359.999`, three worlds in a row: the first downloads, the second and third take everything from the cache (the third proves write -> read -> write -> read);
  - constructor with an existing file and no manifest yet: nothing read (`GetFromCacheCount() == 0` and the file is not modified);
  - `disableCache` part unchanged (file deleted, nothing written).
- [ ] **Step 3: Run** `... unit` — fails.
- [ ] **Step 4: Implement** `DZOMS_Cache`, `CACHE_VERSION = 2`, the client changes. Remove the constructor preload and `m_CacheLoaded` if nothing else needs it; `m_Cache` becomes a `map<string, int>` from range hash to range number for the manifest pass.
- [ ] **Step 5: Run** `... unit` and then all default scenarios (`tools/run-server-tests.sh` without arguments) — all pass.
- [ ] **Step 6: Commit** (`Cache version 2: encrypted hash words, loaded when the manifest arrives`).

### Task 8: docs, client check, package

**Files:**
- Modify: `DESIGN.md` (12: "Planned" -> implemented, with the measured numbers and the protocol 3 deviation; 10.1 and 12.3: positions `* 1024` and why (the float32 argument above), drop the "decoded floats must never be re-encoded" warning; 1 "not in scope" line; 4 `cachekey.bin` in the server profile; 5 `cacheKey` in the example and the field table; 7 `cacheKey` in `DZOMS_Manifest`, protocol 3; 8.1 cache load moves to manifest receipt; 10.2 replaced by the 12.5 format; 11 test list; 15 the two new classes), `README.md` (`cacheKey` row; "deleting `cachekey.bin` makes every player download everything once"), `workshop.txt` if it lists settings.

- [ ] **Step 1: Update the docs.** `tools/check-style.sh` passes.
- [ ] **Step 2: Headless:** `DZOMS_TREE=/tmp/dzoms-stable tools/run-server-tests.sh` — every scenario passes, `boot` module counts include the two new files.
- [ ] **Step 3: Real client, two joins** (dayz-dev `testing/client-testing.md`; `dayz-client-headless.sh status` first, one game at a time): start a server tree with `tests/dze` and the `realdze` config, join with `dayz-client-test.sh` twice. Client log of join 1: `cache: wrote`; join 2: `manifest: N batches, N from the cache` and no `batch ... failed validation`. Delete `profiles/dzo/mapstream/cachekey.bin` in the tree, join a third time: everything downloaded, cache rewritten. Remove the client symlinks afterwards. Report the timings from the client log.
- [ ] **Step 4: Package** with `dayz-mod-pack.sh -C pack build`, run `DZOMS_PACKAGE=1 DZOMS_TREE=/tmp/dzoms-stable tools/run-server-tests.sh boot unit`, print the `publish-hint` command. Do not upload.
- [ ] **Step 5: Commit** (`Docs: cache encryption`).
