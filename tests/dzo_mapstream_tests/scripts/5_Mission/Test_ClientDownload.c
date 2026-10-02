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

	static DZOMS_Manifest MakeManifest(array<ref DZOMS_Batch> batches, array<float> bounds, bool disableCache)
	{
		DZOMS_Manifest m = new DZOMS_Manifest();
		m.viewDistance = 1500;
		m.proxyDistance = 1000;
		m.disableCache = disableCache;
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
		Tick(w, 1000, 1, "no request at 1000");
		Tick(w, 3100, 2, "second manifest request at 3100");

		DZOMS_Manifest invalid = MakeManifest(sb, bo, false);
		invalid.bounds.RemoveOrdered(0);
		w.HandleManifest(invalid, 3200);
		DZOMS_T.Check(w.GetManifest() == null, "invalid manifest ignored");
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

		// empty manifest
		DZOMS_ClientWorld w5 = new DZOMS_ClientWorld(path);
		w5.m_Offline = true;
		w5.Update("0 0 0", 0, true);
		w5.HandleManifest(new DZOMS_Manifest(), 10);
		DZOMS_T.Check(w5.IsComplete(), "empty manifest completes at once");
		DZOMS_T.Check(!FileExist(path), "empty manifest writes no file");

		DZOMS_T.Eq(DZOMS_ClientWorld.CachePathForHost(), "", "no host address on the dedicated server");
		DeleteFile(path);
	}
}
