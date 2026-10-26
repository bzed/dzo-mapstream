class DZOMS_Test_ClientDownload
{
	// 3 server batches of 5 apples at x = 0, 1000, 2000
	static void MakeServer(array<ref DZOMS_Batch> batches, array<float> bounds, float dx0)
	{
		array<ref DZOMS_ObjectEntry> list = new array<ref DZOMS_ObjectEntry>();
		for (int g = 0; g < 3; g++)
		{
			for (int i = 0; i < 5; i++)
			{
				float x = g * 1000 + i * 0.5;
				if (g == 0)
				{
					x += dx0;
				}
				list.Insert(new DZOMS_ObjectEntry("Apple", Vector(x, 10, 0), "0 0 0", 1, 0));
			}
		}
		DZOMS_BatchBuilder.Build(list, 400, batches, bounds);
	}

	static array<int> TestKey()
	{
		return {11, -22, 33, -44, 55, -66, 77, -88};
	}

	static DZOMS_Manifest MakeManifestKey(array<ref DZOMS_Batch> batches, array<float> bounds, array<int> key)
	{
		DZOMS_Manifest m = MakeManifest(batches, bounds, false);
		m.cacheKey.Clear();
		m.cacheKey.InsertAll(key);
		return m;
	}

	static DZOMS_Manifest MakeManifest(array<ref DZOMS_Batch> batches, array<float> bounds, bool disableCache)
	{
		DZOMS_Manifest m = new DZOMS_Manifest();
		m.viewDistance = 1500;
		m.proxyDistance = 1000;
		m.disableCache = disableCache;
		if (!disableCache)
		{
			m.cacheKey.InsertAll(TestKey());
		}
		for (int i = 0; i < bounds.Count(); i++)
		{
			m.bounds.Insert(bounds.Get(i));
		}
		for (int j = 0; j < batches.Count(); j++)
		{
			m.hashes.Insert(DZOMS_BatchHasher.Hash(batches.Get(j)));
		}
		return m;
	}

	static DZOMS_Batch Clone(DZOMS_Batch src)
	{
		DZOMS_Batch c = new DZOMS_Batch();
		c.index = src.index;
		for (int i = 0; i < src.Count(); i++)
		{
			c.Add(src.GetType(i), src.positions.Get(i), src.orientations.Get(i), src.scales.Get(i), src.flags.Get(i));
		}
		return c;
	}

	static int Sent(DZOMS_ClientWorld w)
	{
		return w.m_SentTypes.Count();
	}

	static void Last(DZOMS_ClientWorld w, int type, int value, string what)
	{
		int n = Sent(w);
		if (n == 0)
		{
			DZOMS_T.Check(false, what + ": nothing was sent");
			return;
		}
		DZOMS_T.EqInt(w.m_SentTypes.Get(n - 1), type, what + " (rpc type)");
		DZOMS_T.EqInt(w.m_SentValues.Get(n - 1), value, what + " (value)");
	}

	static void Tick(DZOMS_ClientWorld w, int now, int expectSent, string what)
	{
		w.Update("2000 10 0", now, true);
		DZOMS_T.EqInt(Sent(w), expectSent, what);
	}

	// positions beyond 8192 m: write -> read -> write -> read keeps every hash
	static void BeyondEightKm()
	{
		string far = DZOMS_T.DIR + "/client_cache_far.bin";
		DeleteFile(far);
		array<ref DZOMS_Batch> fb = new array<ref DZOMS_Batch>();
		array<float> fo = new array<float>();
		for (int g = 0; g < 3; g++)
		{
			DZOMS_Batch fbatch = new DZOMS_Batch();
			fbatch.Add("Apple", Vector(8192.021484375 + g * 3000, 10, 15359.999 - g * 7000), "0 0 0", 1, 0);
			fbatch.Add("Apple", Vector(15359.999 - g * 5000, -16384.001, 8192.021484375), "10 20 30", 1.25, 0);
			fb.Insert(fbatch);
			fo.Insert(8192 + g * 3000);
			fo.Insert(10);
			fo.Insert(15359 - g * 7000);
			fo.Insert(100);
		}
		DZOMS_T.EqInt(fb.Count(), 3, "far: 3 batches");

		DZOMS_ClientWorld first = new DZOMS_ClientWorld(far);
		first.m_Offline = true;
		first.Update("0 0 0", 0, true);
		first.HandleManifest(MakeManifest(fb, fo, false), 10);
		for (int i = 0; i < fb.Count(); i++)
		{
			DZOMS_Batch c = Clone(fb.Get(i));
			c.index = i;
			first.HandleBatch(c, 20 + i);
		}
		DZOMS_T.Check(first.IsComplete(), "far: first world downloads everything");
		DZOMS_T.EqInt(first.GetFromCacheCount(), 0, "far: nothing from the cache at first");

		// second world: a 4th batch is new, the 3 old ones come from the cache and are written back with it
		array<ref DZOMS_Batch> fb2 = new array<ref DZOMS_Batch>();
		array<float> fo2 = new array<float>();
		for (int k = 0; k < fb.Count(); k++)
		{
			fb2.Insert(fb.Get(k));
		}
		for (int q = 0; q < fo.Count(); q++)
		{
			fo2.Insert(fo.Get(q));
		}
		DZOMS_Batch extra = Clone(fb.Get(0));
		extra.positions.Set(0, extra.positions.Get(0) + "1 0 0");
		fb2.Insert(extra);
		for (int e = 0; e < 4; e++)
		{
			fo2.Insert(fo.Get(e));
		}
		DZOMS_ClientWorld second = new DZOMS_ClientWorld(far);
		second.m_Offline = true;
		second.Update("0 0 0", 0, true);
		second.HandleManifest(MakeManifest(fb2, fo2, false), 10);
		DZOMS_T.EqInt(second.GetFromCacheCount(), 3, "far: second world takes 3 from the cache");
		DZOMS_Batch extraCopy = Clone(extra);
		extraCopy.index = 3;
		second.HandleBatch(extraCopy, 30);
		DZOMS_T.Check(second.IsComplete(), "far: second world complete");

		// third world: everything, including the words that went file -> memory -> file, from the cache
		DZOMS_ClientWorld third = new DZOMS_ClientWorld(far);
		third.m_Offline = true;
		third.Update("0 0 0", 0, true);
		third.HandleManifest(MakeManifest(fb2, fo2, false), 10);
		DZOMS_T.EqInt(third.GetFromCacheCount(), 4, "far: third world takes all 4 from the cache");
		DZOMS_T.Check(third.IsComplete(), "far: third world complete");
		DeleteFile(far);
	}

	static void Run()
	{
		DZOMS_T.Suite("clientdownload");
		MakeDirectory(DZOMS_T.DIR);
		string path = DZOMS_T.DIR + "/client_cache.bin";
		DeleteFile(path);
		int RM = DZOMS_Rpc.REQUEST_MANIFEST;
		int RB = DZOMS_Rpc.REQUEST_BATCH;

		array<ref DZOMS_Batch> sb = new array<ref DZOMS_Batch>();
		array<float> bo = new array<float>();
		MakeServer(sb, bo, 0);
		DZOMS_T.EqInt(sb.Count(), 3, "3 server batches");
		DZOMS_Manifest good = MakeManifest(sb, bo, false);

		DZOMS_ClientWorld w = new DZOMS_ClientWorld(path);
		w.m_Offline = true;
		w.Update("2000 10 0", 0, false);
		DZOMS_T.EqInt(Sent(w), 0, "nothing sent before IN_GAME");
		Tick(w, 100, 1, "manifest request at 100");
		Last(w, RM, DZOMS_Rpc.PROTOCOL_VERSION, "manifest request");
		Last(w, RM, 3, "manifest request speaks protocol 3");
		Tick(w, 1000, 1, "no request at 1000");
		Tick(w, 3100, 2, "second manifest request at 3100");

		DZOMS_Manifest invalid = MakeManifest(sb, bo, false);
		invalid.bounds.RemoveOrdered(0);
		w.HandleManifest(invalid, 3200);
		DZOMS_T.Check(w.GetManifest() == null, "invalid manifest ignored");
		DZOMS_Manifest nokey = MakeManifest(sb, bo, false);
		nokey.cacheKey.Clear();
		w.HandleManifest(nokey, 3250);
		DZOMS_T.Check(w.GetManifest() == null, "caching on without a key: manifest ignored");
		DZOMS_Manifest keyoff = MakeManifest(sb, bo, true);
		keyoff.cacheKey.InsertAll(TestKey());
		w.HandleManifest(keyoff, 3260);
		DZOMS_T.Check(w.GetManifest() == null, "caching off with a key: manifest ignored");
		DZOMS_Manifest shortkey = MakeManifest(sb, bo, false);
		shortkey.cacheKey.RemoveOrdered(0);
		w.HandleManifest(shortkey, 3270);
		DZOMS_T.Check(w.GetManifest() == null, "7 key words: manifest ignored");
		DZOMS_Manifest nullkey = MakeManifest(sb, bo, false);
		nullkey.cacheKey = null;
		w.HandleManifest(nullkey, 3280);
		DZOMS_T.Check(w.GetManifest() == null, "NULL key: manifest ignored");
		Tick(w, 6200, 3, "third manifest request at 6200");

		w.HandleManifest(good, 7000);
		DZOMS_T.EqInt(Sent(w), 4, "batch requested after the manifest");
		Last(w, RB, 2, "closest batch first");
		DZOMS_T.EqInt(w.GetInFlight(), 2, "batch 2 in flight");
		w.HandleManifest(MakeManifest(sb, bo, false), 7050);
		DZOMS_T.EqInt(Sent(w), 4, "second manifest ignored");

		DZOMS_Batch moved = Clone(sb.Get(2));
		moved.index = 2;
		moved.positions.Set(0, moved.positions.Get(0) + "0.01 0 0");
		w.HandleBatch(moved, 7100);
		DZOMS_T.EqInt(w.GetReceivedCount(), 0, "moved batch rejected");
		DZOMS_T.EqInt(Sent(w), 5, "rejected batch requested again");
		Last(w, RB, 2, "re-request after hash mismatch");

		DZOMS_Batch garbage = Clone(sb.Get(2));
		garbage.index = 2;
		garbage.typeIndex.Set(1, 99);
		w.HandleBatch(garbage, 7150);
		DZOMS_T.EqInt(w.GetReceivedCount(), 0, "garbage batch rejected");
		DZOMS_T.EqInt(Sent(w), 6, "garbage batch requested again");

		DZOMS_Batch ok2 = Clone(sb.Get(2));
		ok2.index = 2;
		w.HandleBatch(ok2, 7200);
		DZOMS_T.EqInt(w.GetReceivedCount(), 1, "correct batch received");
		DZOMS_T.EqInt(w.GetClientBatch(2).state, DZOMS_ClientBatch.UNLOADED, "received batch is UNLOADED");
		DZOMS_T.EqInt(Sent(w), 7, "next batch requested");
		Last(w, RB, 1, "next closest");

		DZOMS_Batch far7 = Clone(sb.Get(2));
		far7.index = 7;
		w.HandleBatch(far7, 7210);
		DZOMS_Batch dup2 = Clone(sb.Get(2));
		dup2.index = 2;
		w.HandleBatch(dup2, 7220);
		DZOMS_T.EqInt(Sent(w), 7, "index 7 and a duplicate are ignored");
		DZOMS_T.EqInt(w.GetReceivedCount(), 1, "still one batch");

		// timeouts, request 1 was sent at t0
		int t0 = 7200;
		Tick(w, t0 + 4999, 7, "no resend at +4999");
		Tick(w, t0 + 5000, 8, "resend at +5000");
		Tick(w, t0 + 10000, 9, "resend at +10000");
		Tick(w, t0 + 15000, 10, "resend at +15000");
		Tick(w, t0 + 20000, 11, "resend at +20000");
		Tick(w, t0 + 25000, 11, "5th timeout: no resend");
		Tick(w, t0 + 40000, 11, "paused for 30 s");
		Tick(w, t0 + 55000, 12, "continues after the pause");
		Last(w, RB, 1, "request 1 again");

		DZOMS_Batch ok1 = Clone(sb.Get(1));
		ok1.index = 1;
		w.HandleBatch(ok1, t0 + 55010);
		Last(w, RB, 0, "last batch requested");
		DZOMS_Batch ok0 = Clone(sb.Get(0));
		ok0.index = 0;
		w.HandleBatch(ok0, t0 + 55020);
		DZOMS_T.Check(w.IsComplete(), "complete");
		DZOMS_T.EqInt(w.GetDownloadedCount(), 3, "3 downloaded");
		DZOMS_T.Check(FileExist(path), "cache file written");

		// the constructor does not read the cache (the key arrives with the manifest)
		string fileBefore = DZOMS_Test_Config.ReadText(path);
		DZOMS_ClientWorld w1b = new DZOMS_ClientWorld(path);
		w1b.m_Offline = true;
		w1b.Update("2000 10 0", 0, true);
		DZOMS_T.EqInt(w1b.GetFromCacheCount(), 0, "constructor reads nothing");
		DZOMS_T.Check(FileExist(path), "constructor leaves the file");
		DZOMS_T.Check(DZOMS_Test_Config.ReadText(path) == fileBefore, "constructor leaves the file unchanged");

		// same server again: everything from the cache
		DZOMS_ClientWorld w2 = new DZOMS_ClientWorld(path);
		w2.m_Offline = true;
		w2.Update("2000 10 0", 0, true);
		w2.HandleManifest(MakeManifest(sb, bo, false), 10);
		DZOMS_T.EqInt(w2.GetFromCacheCount(), 3, "3 batches from the cache");
		DZOMS_T.Check(w2.IsComplete(), "complete from the cache");
		DZOMS_T.EqInt(Sent(w2), 1, "only the manifest was requested");
		DZOMS_T.EqInt(w2.GetDownloadedCount(), 0, "nothing downloaded");

		// batch 0 changed on the server
		array<ref DZOMS_Batch> sb3 = new array<ref DZOMS_Batch>();
		array<float> bo3 = new array<float>();
		MakeServer(sb3, bo3, 0.5);
		DZOMS_ClientWorld w3 = new DZOMS_ClientWorld(path);
		w3.m_Offline = true;
		w3.Update("0 10 0", 0, true);
		w3.HandleManifest(MakeManifest(sb3, bo3, false), 10);
		DZOMS_T.EqInt(w3.GetFromCacheCount(), 2, "2 batches from the cache");
		DZOMS_T.Check(!w3.IsComplete(), "not complete");
		Last(w3, RB, 0, "changed batch requested");
		DZOMS_Batch changed0 = Clone(sb3.Get(0));
		changed0.index = 0;
		w3.HandleBatch(changed0, 30);
		DZOMS_T.Check(w3.IsComplete(), "world with a changed batch completes");
		DZOMS_T.EqInt(w3.GetDownloadedCount(), 1, "1 downloaded");
		DZOMS_ClientWorld w3b = new DZOMS_ClientWorld(path);
		w3b.m_Offline = true;
		w3b.Update("0 10 0", 0, true);
		w3b.HandleManifest(MakeManifest(sb3, bo3, false), 10);
		DZOMS_T.EqInt(w3b.GetFromCacheCount(), 3, "mixed write: all 3 from the cache");
		DZOMS_T.Check(w3b.IsComplete(), "mixed write: complete");

		// another key: nothing from the cache, everything requested, the file is rewritten with the new key
		array<int> key2 = {9, 8, 7, 6, 5, 4, 3, 2};
		DZOMS_ClientWorld wk = new DZOMS_ClientWorld(path);
		wk.m_Offline = true;
		wk.Update("0 10 0", 0, true);
		wk.HandleManifest(MakeManifestKey(sb3, bo3, key2), 10);
		DZOMS_T.EqInt(wk.GetFromCacheCount(), 0, "other key: nothing from the cache");
		DZOMS_T.Check(!wk.IsComplete(), "other key: not complete");
		for (int ki = 0; ki < 3; ki++)
		{
			DZOMS_Batch kb = Clone(sb3.Get(ki));
			kb.index = ki;
			wk.HandleBatch(kb, 40 + ki);
		}
		DZOMS_T.Check(wk.IsComplete(), "other key: complete after the downloads");
		DZOMS_ClientWorld wk2 = new DZOMS_ClientWorld(path);
		wk2.m_Offline = true;
		wk2.Update("0 10 0", 0, true);
		wk2.HandleManifest(MakeManifestKey(sb3, bo3, key2), 10);
		DZOMS_T.EqInt(wk2.GetFromCacheCount(), 3, "new key: all 3 from the rewritten cache");

		// a version 1 file: ignored, replaced by a version 2 file
		FileSerializer v1 = new FileSerializer();
		if (v1.Open(path, FileMode.WRITE))
		{
			v1.Write(1);
			v1.Write(3);
			v1.Write(sb3.Get(0));
			v1.Write(sb3.Get(1));
			v1.Write(sb3.Get(2));
			v1.Close();
		}
		DZOMS_ClientWorld wv = new DZOMS_ClientWorld(path);
		wv.m_Offline = true;
		wv.Update("0 10 0", 0, true);
		wv.HandleManifest(MakeManifest(sb3, bo3, false), 10);
		DZOMS_T.EqInt(wv.GetFromCacheCount(), 0, "version 1 file: nothing from the cache");
		for (int vi = 0; vi < 3; vi++)
		{
			DZOMS_Batch vb = Clone(sb3.Get(vi));
			vb.index = vi;
			wv.HandleBatch(vb, 50 + vi);
		}
		DZOMS_T.Check(wv.IsComplete(), "version 1 file: complete after the downloads");
		DZOMS_ClientWorld wv2 = new DZOMS_ClientWorld(path);
		wv2.m_Offline = true;
		wv2.Update("0 10 0", 0, true);
		wv2.HandleManifest(MakeManifest(sb3, bo3, false), 10);
		DZOMS_T.EqInt(wv2.GetFromCacheCount(), 3, "version 2 file written after the downloads");

		// restore the file the following parts expect: the cache of sb (batch 0 unchanged)
		DeleteFile(path);
		DZOMS_ClientWorld wr = new DZOMS_ClientWorld(path);
		wr.m_Offline = true;
		wr.Update("0 10 0", 0, true);
		wr.HandleManifest(MakeManifest(sb, bo, false), 10);
		for (int ri = 0; ri < 3; ri++)
		{
			DZOMS_Batch rb = Clone(sb.Get(ri));
			rb.index = ri;
			wr.HandleBatch(rb, 60 + ri);
		}

		// disableCache: file deleted, nothing used, nothing written
		DZOMS_ClientWorld w4 = new DZOMS_ClientWorld(path);
		w4.m_Offline = true;
		w4.Update("2000 10 0", 0, true);
		w4.HandleManifest(MakeManifest(sb, bo, true), 10);
		DZOMS_T.Check(!FileExist(path), "disableCache deletes the cache file");
		DZOMS_T.EqInt(w4.GetFromCacheCount(), 0, "nothing used from the cache");
		for (int i = 0; i < 3; i++)
		{
			DZOMS_Batch c = Clone(sb.Get(i));
			c.index = i;
			w4.HandleBatch(c, 20 + i);
		}
		DZOMS_T.Check(w4.IsComplete(), "disableCache world complete");
		DZOMS_T.Check(!FileExist(path), "disableCache: still no file");

		BeyondEightKm();

		// empty manifest
		DZOMS_ClientWorld w5 = new DZOMS_ClientWorld(path);
		w5.m_Offline = true;
		w5.Update("0 0 0", 0, true);
		w5.HandleManifest(MakeManifest(new array<ref DZOMS_Batch>(), new array<float>(), false), 10);
		DZOMS_T.Check(w5.IsComplete(), "empty manifest completes at once");
		DZOMS_T.Check(!FileExist(path), "empty manifest writes no file");

		DZOMS_T.Eq(DZOMS_ClientWorld.CachePathForHost(), "", "no host address on the dedicated server");
		DeleteFile(path);
	}
}
