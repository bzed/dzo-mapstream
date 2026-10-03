class DZOMS_Test_Requests
{
	static void Run()
	{
		DZOMS_T.Suite("requests");

		// rate limiter on its own
		DZOMS_RateLimiter rl = new DZOMS_RateLimiter();
		DZOMS_T.Check(rl.AllowManifest("a", 0), "manifest at 0");
		DZOMS_T.Check(!rl.AllowManifest("a", 500), "manifest at 500 refused");
		DZOMS_T.Check(rl.AllowManifest("a", 1000), "manifest at 1000");
		DZOMS_T.Check(rl.AllowManifest("b", 500), "other id at 500");
		int allowed = 0;
		for (int i = 0; i < 25; i++)
		{
			if (rl.AllowBatch("c", 0))
			{
				allowed++;
			}
		}
		DZOMS_T.EqInt(allowed, 20, "25 batch requests at 0 -> 20");
		DZOMS_T.Check(rl.AllowBatch("c", 50), "one more after 50 ms");
		DZOMS_T.Check(!rl.AllowBatch("c", 50), "and not two");
		allowed = 0;
		for (int j = 0; j < 25; j++)
		{
			if (rl.AllowBatch("c", 60000))
			{
				allowed++;
			}
		}
		DZOMS_T.EqInt(allowed, 20, "capacity is 20 after a long pause");
		DZOMS_T.EqInt(rl.Count(), 3, "3 ids tracked");
		rl.Remove("b");
		DZOMS_T.EqInt(rl.Count(), 2, "Remove");
		rl.Sweep(180001);
		DZOMS_T.EqInt(rl.Count(), 0, "Sweep drops idle ids");

		// requests against a world with 2 batches
		string mapsDir = DZOMS_T.DIR + "/DZE_req";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(mapsDir);
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 600";
		EditorSaveData data = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(data, "Apple", at, "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(data, "Apple", at + "500 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.Save(data, mapsDir + "/req.dze");

		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		DZOMS_MapEntry e = new DZOMS_MapEntry();
		e.file = "req.dze";
		c.maps.Insert(e);
		c.Normalize();
		DZOMS_ServerWorld w = new DZOMS_ServerWorld(c, mapsDir, new DZOMS_ObjectProbe());
		w.Load();
		int pv = DZOMS_Rpc.PROTOCOL_VERSION;
		DZOMS_T.EqInt(w.GetBatches().Count(), 2, "2 batches");

		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_MANIFEST, pv, 0), DZOMS_ServerWorld.ANSWER_MANIFEST, "manifest answered");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_MANIFEST, pv, 500), DZOMS_ServerWorld.ANSWER_NONE, "manifest rate limited");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_MANIFEST, pv + 1, 2000), DZOMS_ServerWorld.ANSWER_NONE, "wrong protocol");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_BATCH, -1, 2000), DZOMS_ServerWorld.ANSWER_NONE, "batch -1");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_BATCH, 2, 2000), DZOMS_ServerWorld.ANSWER_NONE, "batch == count");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.REQUEST_BATCH, 1, 2000), DZOMS_ServerWorld.ANSWER_BATCH, "batch 1");
		DZOMS_T.EqInt(w.HandleRequest("idA", DZOMS_Rpc.BATCH, 0, 2000), DZOMS_ServerWorld.ANSWER_NONE, "server->client id ignored");
		DZOMS_T.EqInt(w.HandleRequest("idA", 12345, 0, 2000), DZOMS_ServerWorld.ANSWER_NONE, "unknown id ignored");

		int answered = 0;
		for (int k = 0; k < 30; k++)
		{
			if (w.HandleRequest("idC", DZOMS_Rpc.REQUEST_BATCH, 0, 5000) == DZOMS_ServerWorld.ANSWER_BATCH)
			{
				answered++;
			}
		}
		DZOMS_T.EqInt(answered, 20, "30 batch requests at once -> 20 answered");
		DZOMS_T.EqInt(w.GetRateLimiter().Count(), 2, "two ids tracked");
		w.OnPlayerDisconnected("idC");
		DZOMS_T.EqInt(w.GetRateLimiter().Count(), 1, "disconnect removes the id");
		w.Destroy();
	}
}
