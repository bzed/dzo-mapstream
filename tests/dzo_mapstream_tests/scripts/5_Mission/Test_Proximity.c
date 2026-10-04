// Proximity networking (DESIGN 13): protocol messages, server state, promotion and demotion.
class DZOMS_Test_Proximity
{
	// a server world with n local apples 10 m apart (class "Apple" is local without rules)
	static DZOMS_ServerWorld MakeWorld(string name, int n, vector at)
	{
		string mapsDir = DZOMS_T.DIR + "/DZE_" + name;
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(mapsDir);
		EditorSaveData data = new EditorSaveData();
		for (int i = 0; i < n; i++)
		{
			DZOMS_TestFixtures.AddObject(data, "Apple", at + Vector(i * 10, 0, 0), "0 0 0", 1, 0);
		}
		DZOMS_TestFixtures.Save(data, mapsDir + "/" + name + ".dze");
		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		DZOMS_MapEntry e = new DZOMS_MapEntry();
		e.file = name + ".dze";
		c.maps.Insert(e);
		c.Normalize();
		DZOMS_ServerWorld w = new DZOMS_ServerWorld(c, mapsDir, new DZOMS_ObjectProbe());
		w.m_Offline = true;
		w.Load();
		return w;
	}

	static void RunProtocol()
	{
		DZOMS_T.EqInt(DZOMS_Rpc.PROTOCOL_VERSION, 2, "protocol version 2");
		DZOMS_T.EqInt(DZOMS_Rpc.OBJECTS_NETWORKED, 1146768645, "OBJECTS_NETWORKED id");
		DZOMS_T.EqInt(DZOMS_Rpc.OBJECTS_LOCAL, 1146768646, "OBJECTS_LOCAL id");
		DZOMS_T.EqInt(DZOMS_Rpc.LAST, DZOMS_Rpc.OBJECTS_LOCAL, "LAST covers the new ids");

		DZOMS_IdList ids = new DZOMS_IdList();
		ids.Add(2, 7);
		ids.Add(0, 1);
		DZOMS_T.EqInt(ids.Count(), 2, "id list count");
		DZOMS_T.Check(ids.IsValid(), "id list valid");
		DZOMS_T.EqInt(ids.batch.Get(0), 2, "id list batch");
		DZOMS_T.EqInt(ids.index.Get(0), 7, "id list index");

		DZOMS_IdList skewed = new DZOMS_IdList();
		skewed.batch.Insert(1);
		DZOMS_T.Check(!skewed.IsValid(), "unequal lengths are invalid");
		DZOMS_IdList big = new DZOMS_IdList();
		for (int i = 0; i < 500; i++)
		{
			big.Add(0, i);
		}
		DZOMS_T.Check(big.IsValid(), "500 ids are valid");
		big.Add(0, 500);
		DZOMS_T.Check(!big.IsValid(), "501 ids are invalid");

		// round trip through a FileSerializer (the payload classes are flat arrays)
		string path = DZOMS_T.DIR + "/idlist.bin";
		FileSerializer ser = new FileSerializer();
		if (ser.Open(path, FileMode.WRITE))
		{
			ser.Write(ids);
			ser.Close();
		}
		DZOMS_IdList back = new DZOMS_IdList();
		bool ok = false;
		FileSerializer ser2 = new FileSerializer();
		if (ser2.Open(path, FileMode.READ))
		{
			ok = ser2.Read(back);
			ser2.Close();
		}
		DZOMS_T.Check(ok, "id list read back");
		DZOMS_T.EqInt(back.Count(), 2, "id list round trip count");
		if (back.Count() == 2)
		{
			DZOMS_T.EqInt(back.batch.Get(1), 0, "id list round trip batch");
			DZOMS_T.EqInt(back.index.Get(1), 1, "id list round trip index");
		}
		DeleteFile(path);
	}

	static void RunServerState()
	{
		DZOMS_ServerWorld w = MakeWorld("prox_state", 4, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -300");
		DZOMS_T.EqInt(w.GetObjectCount(), 4, "4 objects");
		DZOMS_T.Check(!w.IsPromoted(0), "not promoted at the start");
		w.MarkPromoted(1, true);
		w.MarkPromoted(3, true);
		DZOMS_T.Check(w.IsPromoted(1) && w.IsPromoted(3) && !w.IsPromoted(0), "promoted flags");
		DZOMS_T.EqInt(w.GetPromotedCount(), 2, "promoted count");
		w.MarkPromoted(-1, true);
		w.MarkPromoted(4, true);
		DZOMS_T.EqInt(w.GetPromotedCount(), 2, "invalid indices are ignored");
		DZOMS_T.Check(!w.IsPromoted(-1) && !w.IsPromoted(4), "invalid indices are never promoted");
		w.MarkPromoted(1, false);
		DZOMS_T.EqInt(w.GetPromotedCount(), 1, "demoted count");

		// the full state a joining client gets
		w.m_BroadcastTypes.Clear();
		w.m_BroadcastLists.Clear();
		w.SendFullSet(null);
		DZOMS_T.EqInt(w.m_BroadcastTypes.Count(), 1, "full set: one message");
		if (w.m_BroadcastLists.Count() == 1)
		{
			DZOMS_T.EqInt(w.m_BroadcastTypes.Get(0), DZOMS_Rpc.OBJECTS_NETWORKED, "full set: networked message");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).Count(), 1, "full set: one id");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).batch.Get(0), w.GetObjectBatch(3), "full set: batch of object 3");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).index.Get(0), w.GetObjectIndexInBatch(3), "full set: index of object 3");
		}

		// chunks of at most 500
		DZOMS_IdList many = new DZOMS_IdList();
		for (int i = 0; i < 1200; i++)
		{
			many.Add(0, i);
		}
		w.m_BroadcastTypes.Clear();
		w.m_BroadcastLists.Clear();
		w.BroadcastNetworked(many);
		DZOMS_T.EqInt(w.m_BroadcastLists.Count(), 3, "1200 ids go out in 3 messages");
		if (w.m_BroadcastLists.Count() == 3)
		{
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).Count(), 500, "chunk 1 has 500");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(1).Count(), 500, "chunk 2 has 500");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(2).Count(), 200, "chunk 3 has 200");
			DZOMS_T.EqInt(w.m_BroadcastTypes.Get(2), DZOMS_Rpc.OBJECTS_NETWORKED, "chunks use the networked id");
		}
		w.m_BroadcastTypes.Clear();
		w.m_BroadcastLists.Clear();
		w.BroadcastLocal(ids5());
		DZOMS_T.EqInt(w.m_BroadcastTypes.Count(), 1, "local broadcast: one message");
		if (w.m_BroadcastTypes.Count() == 1)
		{
			DZOMS_T.EqInt(w.m_BroadcastTypes.Get(0), DZOMS_Rpc.OBJECTS_LOCAL, "local broadcast id");
		}
		w.Destroy();
	}

	static DZOMS_IdList ids5()
	{
		DZOMS_IdList l = new DZOMS_IdList();
		for (int i = 0; i < 5; i++)
		{
			l.Add(1, i);
		}
		return l;
	}

	static void Run()
	{
		DZOMS_T.Suite("proximity");
		RunProtocol();
		RunServerState();
	}
}
