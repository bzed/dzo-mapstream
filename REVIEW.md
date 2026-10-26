# Review: branch `cache-encryption`

Review of the 8 commits on `cache-encryption` against `main` (d286a7c..00156db, 23 files,
+1429/-174). The branch implements DESIGN.md section 12: the client cache file is
ChaCha20-encrypted with a server key sent in the manifest (protocol 3, cache version 2),
and stores the batches' hash words instead of batch objects, which also makes loading
faster. Positions in the hash words change from millimetres to 1/1024 m so a batch
decoded from its words encodes to the same words at any map size.

Scope of the review: every changed file read end to end, the Python reference scripts run
and compared against the test expectations, the Enforce Script pitfalls of DESIGN.md
section 14 checked against the new code, `tools/check-style.sh` run, and the bundled
1.30 API (`dayz-api.py`) used to verify every function and constant the new code calls.
The headless server tests were **not** run (no server tree in this environment).

## Verdict

The branch is well implemented. The crypto is verified against RFC 8439 and Python
references, the cache read path is defensive at every step, the client integration keeps
the hash words unchanged end to end, and the test coverage is broad. No blocking issues.
Four minor findings below, none of them bugs.

## What was verified

- **ChaCha20** (`DZOMS_ChaCha20.c`): RFC 8439 2.3.2 block function on int words. The 10
  double rounds are written out on locals with rotations as
  `(x << n) | ((x >> (32 - n)) & ((1 << n) - 1))`, which is the correct workaround for
  Enforce's arithmetic `>>`. The generated lines match `tests/python/chacha_vectors.py
  rounds` exactly. `XorWords` computes one block per 16 words into a reused array, block
  counter `i >> 4`, keystream word `i & 15`. The test vectors (RFC block as signed ints,
  40-word partial case, XOR-twice restore, 0 and 33 words) match the Python reference
  output, which I ran.
- **SHA-256 over a range** (`DZOMS_Sha256.c`): `HashWords(words, start, count)` copies
  only the range, pads with `1 << 31` and the 64-bit bit length of `count` (not the full
  array). The existing `HashWords(words)` delegates with `(0, words.Count())`. Tests pin
  ranges of 1, 13, 14, 16, 17 words embedded at offset 5 between junk words, plus a
  0-word range giving the empty hash.
- **Positions in 1/1024 m** (`DZOMS_BatchHasher.c`): the float32 round-trip argument is
  sound — below 8192 m the word is < 2^23 and converts to float exactly; from 8192 m on
  `x * 1024` is an integer with at most 24 significant bits. `Test_Hasher` checks
  324000+ values from -20000 to 20000 plus the 8192 m, 16384 m and negative edges, and
  the reference words/hashes match the updated `sha_vectors.py` output (ran it).
- **`DZOMS_BatchHasher.Decode`**: every count and length is checked against the words
  left before use. The `n * 9 != end - pos` check is guarded by `n > (end - pos) / 9`
  first (short-circuit `||`), so the multiply cannot overflow. Type names are decoded
  big-endian 4-per-word with `AsciiToString()` per byte. The result passes `IsValid()`.
  Rejection tests cover one-word-short/long, bad type lengths and counts, negative
  values, huge counts, and out-of-range type indices. Positions beyond 8192 m survive
  `Words(Decode(Words(b))) == Words(b)`.
- **`DZOMS_Cache` (version 2)**: write builds `[CHECK_WORD, batchCount, count_i,
  words_i...]`, encrypts with `XorWords`, writes `int version, 3x int nonce,
  array<int>`. Read checks version, decrypts, verifies the check word (wrong key ->
  `REASON_KEY`), bounds-checks the batch count against `MAX_BATCHES`, every per-batch
  word count against the remaining words, and requires `pos == size` (no leftovers). All
  failure paths clear the three output arrays — no partial use. Tests cover round trip,
  wrong key, one flipped word (only that batch's hash differs), version 1 file, version
  +1, header-only, claimed-count mismatch, word count past the end, negative counts,
  leftover word, 50 random ints, empty file, 0 batches, and write to a missing directory.
- **`DZOMS_CacheKey`**: `Parse` validates 64 hex chars nibble by nibble (also rejects
  non-hex), big endian, `ffffffff` -> -1 (verified the `<< 4` wrap). `RandomWords`
  mixes two `Math.RandomInt(0, int.MAX)` per word with `TickCount`, UTC date/time and
  `g_Game.GetTime()` through one ChaCha20 block; never calls `Math.Randomize(-1)`.
  `Resolve` uses a valid configured key (file untouched), else the key file (8 words,
  not all zero), else generates and writes one. Config tests cover parse (lower/upper
  case, 63/65 chars, `g`, space, empty), random words (count, not all zero, two differ),
  fresh dir (written, same on reload), 3-word and all-zero files (replaced, stable
  after), garbage text file (new key), configured key (used, no file), invalid
  configured key (error logged, generated key, `m_LoadFailed` stays false).
- **Manifest and protocol**: `DZOMS_Manifest.cacheKey` (8 ints, empty when
  `disableCache`), `IsValid()` requires the count to match `disableCache`.
  `PROTOCOL_VERSION` 3, `CACHE_VERSION` 2. The server fills the key from
  `m_Config.m_CacheKey` (random fallback for test configs without `Load`). Client tests
  reject manifests with: caching on + 0/7/NULL key words, caching off + 8 words.
- **Client integration** (`DZOMS_ClientWorld.c`): the constructor no longer reads the
  cache; `HandleManifest` reads it with `m.cacheKey`, hashes each cached range in place
  (`HashWords(words, start, count)`, no copy) into `map<string hash, int range>`, decodes
  only ranges whose hash is in the manifest, copies the words into a per-batch array, and
  drops the big cache arrays. `HandleBatch` keeps the words it hashes. `Complete` writes
  every batch's stored words with the manifest key and a fresh `RandomWords(3)` nonce.
  `disableCache` deletes the file. Tests cover the full download flow, cache hits,
  changed batch, mixed write, key change (re-download + rewrite), version 1 file
  (rejected + replaced), three joins with positions beyond 8192 m (write -> read ->
  write -> read), constructor-does-not-read, and empty manifest.
- **API verification** (`dayz-api.py`, vanilla 1.30): `string.ToLower()`/`ToUpper()`
  return `int` and modify in place (so `c.ToLower()` in `Parse` and `hex.ToUpper()` in
  the test are correct — consistent with the rest of the codebase, e.g.
  `DZOMS_NetworkRules.c:69`). `Math.RandomInt(int, int)` is min-inclusive/max-exclusive.
  `int.MAX = 2147483647`, `int.MIN = -2147483648`. `TickCount(int)`,
  `GetYearMonthDayUTC(out int, out int, out int)`, `GetHourMinuteSecondUTC(...)`,
  `int.AsciiToString()`, `g_Game.GetTime()` all exist.
- **Style**: `tools/check-style.sh` passes (no CR, no trailing whitespace).
- **Docs**: DESIGN.md sections 1, 4, 5, 7, 8, 10.1, 10.2, 11, 12 (all), 15 updated and
  consistent with the code; the protocol-3 deviation from the original "bump to 2" plan
  is noted in 12.5. README adds `cacheKey` to the example and the settings table.

## Findings

### 1. Minor: `HandleManifest` checks `IsValid()` before the protocol version

`DZOMS_ClientWorld.c:1194-1202` — a manifest from an older server (protocol 2, no
`cacheKey`) is rejected by `IsValid()` with "invalid manifest ignored" instead of the
more informative "manifest has protocol 2, this client speaks 3". Functionally correct
(mismatched manifests are rejected either way); the log line is just less helpful for
diagnosing a version mismatch. This order predates the branch.

### 2. Minor: `Test_ClientDownload` "constructor leaves the file" assertion is weak

`Test_ClientDownload.c:229-234` — the test reads `fileBefore` but never compares it. It
only checks `FileExist(path)`, so a constructor that truncated or rewrote the file would
still pass. The `GetFromCacheCount() == 0` check covers the important part (nothing was
read); the file-content check is a missed assertion, not a code bug.

### 3. Minor: `Test_Config` "key_garbage" case does not verify file replacement

`Test_Config.c:181-184` — the "key_short" and "key_zero" cases do a second `Load` and
check the new key is stable (the broken file was replaced). The "key_garbage" case (text
in `cachekey.bin`) only checks that a new 8-word non-zero key is returned, not that the
file was overwritten. The code path is the same (`Resolve` falls through to generate +
write), so this is a test gap, not a code bug.

### 4. Minor: `DZOMS_Sha256.HashWords` reserve hint is one word short

`DZOMS_Sha256.c:30` — `p.Reserve(count + 17)` but the maximum padded size is
`count + 18` words (count + 1 for the `0x80` word + up to 15 padding + 2 length words).
`Reserve` is only a capacity hint in Enforce; the array grows as needed. Harmless.

## Notes (not findings)

- **Position encoding change invalidates all existing caches** — intentional. The
  protocol bump to 3 makes old clients reject the new manifest, and the cache version
  bump to 2 makes new clients reject old cache files. Every client re-downloads once
  after this update.
- **`DZOMS_CacheKey.RandomWords` is not cryptographic quality** — documented in DESIGN
  12.4 and the code comment. The threat model (a cache file copied off a disk is useless
  without joining the server) does not require it: anyone who can join gets the key from
  the manifest anyway. An admin-set `cacheKey` is the strong option.
- **Nonce reuse**: a fresh `RandomWords(3)` nonce per cache write. 96 bits of
  non-cryptographic randomness; collision probability is negligible for the number of
  writes a client performs. Documented in DESIGN 12.4.
- **`DZOMS_Cache.Read` accepts 0-word batches structurally** — a batch with
  `wordCount = 0` passes the structural check but `Decode` rejects it (`count < 2`), so
  `HandleManifest` skips it and the batch is downloaded. Defense in depth works.
- **An admin-configured all-zero `cacheKey` is accepted** — `Parse` succeeds and
  `Resolve` returns it before the `Usable` check. The `Usable` check only guards the
  *file* path (a corrupted all-zero file). An explicit admin choice is respected.
- **`Math.Round` rounding mode on exact halves** is not pinned against the Python
  `rnd()` (halves away from zero) — the reference batch has no half-value positions, so
  the tests pass regardless. Client and server both use `Math.Round`, so they always
  agree; only the Python reference could theoretically differ.

## Not verified

- The headless server tests (`tools/run-server-tests.sh`) were not run — no server tree
  in this environment. The plan's Task 8 step 2-4 (headless boot, real client joins,
  packaging) should be run before merge.
- The real-client measurements quoted in DESIGN 12.2 (three joins on 1.30) were not
  reproduced here.
