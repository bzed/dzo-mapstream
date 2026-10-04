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

		DZOMS_T.Check(!ids.HasNetIds(), "plain ids carry no network ids");
		DZOMS_IdList withNet = new DZOMS_IdList();
		withNet.AddNet(3, 4, 11, 12);
		DZOMS_T.Check(withNet.IsValid() && withNet.HasNetIds(), "ids with network ids are valid");
		DZOMS_T.EqInt(withNet.netLow.Get(0), 11, "network id low");
		DZOMS_T.EqInt(withNet.netHigh.Get(0), 12, "network id high");
		DZOMS_IdList skewedNet = new DZOMS_IdList();
		skewedNet.AddNet(1, 1, 1, 1);
		skewedNet.netHigh.Clear();
		DZOMS_T.Check(!skewedNet.IsValid(), "network id arrays of unequal length are invalid");
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
		DZOMS_T.Check(w.Promote(3), "object 3 promoted for real");
		int nl, nh;
		DZOMS_T.Check(w.GetNetworkId(3, nl, nh) && (nl != 0 || nh != 0), "promoted object has a network id");
		DZOMS_T.Check(!w.GetNetworkId(0, nl, nh), "local object has none");
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
			DZOMS_T.Check(w.m_BroadcastLists.Get(0).HasNetIds(), "full set carries network ids");
			int fl, fh;
			w.GetNetworkId(3, fl, fh);
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).netLow.Get(0), fl, "full set: network id low of object 3");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(0).netHigh.Get(0), fh, "full set: network id high of object 3");
		}

		// chunks of at most 500
		DZOMS_IdList many = new DZOMS_IdList();
		for (int i = 0; i < 1200; i++)
		{
			many.AddNet(0, i, i + 1, i + 2);
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
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(1).netLow.Get(0), 501, "chunk 2 keeps its network ids (low)");
			DZOMS_T.EqInt(w.m_BroadcastLists.Get(2).netHigh.Get(199), 1201, "chunk 3 keeps its network ids (high)");
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

	static DZOMS_IdList One(int b, int i)
	{
		DZOMS_IdList l = new DZOMS_IdList();
		l.Add(b, i);
		return l;
	}

	static DZOMS_IdList OneNet(int b, int i, int low, int high)
	{
		DZOMS_IdList l = new DZOMS_IdList();
		l.AddNet(b, i, low, high);
		return l;
	}

	// a loaded client world: batch 0 = 6 apples (0.5 m apart), batch 1 = 3 apples 3 km away
	static DZOMS_ClientWorld MakeClient(vector base)
	{
		array<ref DZOMS_ObjectEntry> list = new array<ref DZOMS_ObjectEntry>();
		for (int i = 0; i < 6; i++)
		{
			list.Insert(new DZOMS_ObjectEntry("Apple", base + Vector(i * 0.5, 0, 0), "0 0 0", 1, 0));
		}
		for (int j = 0; j < 3; j++)
		{
			list.Insert(new DZOMS_ObjectEntry("Apple", base + Vector(3000 + j * 0.5, 0, 0), "0 0 0", 1, 0));
		}
		array<ref DZOMS_Batch> sb = new array<ref DZOMS_Batch>();
		array<float> bo = new array<float>();
		DZOMS_BatchBuilder.Build(list, 400, sb, bo);
		DZOMS_Manifest manifest = DZOMS_Test_ClientDownload.MakeManifest(sb, bo, true);
		DZOMS_ClientWorld w = new DZOMS_ClientWorld("");
		w.m_Offline = true;
		w.Update(base, DZOMS_Test_ClientSpawn.s_Time, true);
		w.HandleManifest(manifest, DZOMS_Test_ClientSpawn.s_Time);
		for (int k = 0; k < 2; k++)
		{
			DZOMS_Batch c = DZOMS_Test_ClientDownload.Clone(sb.Get(k));
			c.index = k;
			w.HandleBatch(c, DZOMS_Test_ClientSpawn.s_Time);
		}
		return w;
	}

	static void RunClientSwap()
	{
		DZOMS_Test_ClientSpawn.s_Time = 1000;
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -700";

		// announced before the batch spawns: no local copy for that entry
		DZOMS_ClientWorld w = MakeClient(base);
		// the batch with 6 entries is the one near the camera
		int nearIndex = 0;
		if (w.GetClientBatch(1).data.Count() == 6)
		{
			nearIndex = 1;
		}
		DZOMS_ClientBatch b0 = w.GetClientBatch(nearIndex);
		w.HandleNetworked(OneNet(nearIndex, 2, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.Check(w.IsAnnouncedNetworked(nearIndex, 2), "announced id is in the set");
		DZOMS_T.EqInt(w.GetNetworkedIdCount(), 1, "one id announced");
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.EqInt(DZOMS_Test_ClientSpawn.Live(b0), 5, "announced entry is not spawned locally, the other 5 are");
		DZOMS_T.Check(b0.objects.Get(2) == null, "slot of the announced entry is empty");

		// announced after the spawn, networked instance present: local copy goes
		Object twin = g_Game.CreateObjectEx("Apple", base + "1.5 0 0", ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		DZOMS_T.Check(twin != null, "networked twin created");
		Object copy3 = b0.objects.Get(3);
		DZOMS_T.Check(copy3 != null, "entry 3 has a local copy");
		// a decoy: another networked apple at the same place (like a hidden world object), other ids
		Object decoy = g_Game.CreateObjectEx("Apple", base + "2.5 0 0", ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		Object copy5 = b0.objects.Get(5);
		w.HandleNetworked(OneNet(nearIndex, 5, 7001, 0), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(b0.objects.Get(5) == copy5, "a decoy at the same place does not delete the local copy");
		DZOMS_T.EqInt(w.GetPendingSwapCount(), 1, "decoy: still pending");
		w.HandleLocal(One(nearIndex, 5), DZOMS_Test_ClientSpawn.s_Time);
		g_Game.ObjectDelete(decoy);

		int twinLow, twinHigh;
		twin.GetNetworkID(twinLow, twinHigh);
		DZOMS_T.Check(twinLow != 0 || twinHigh != 0, "the twin has a network id");
		w.HandleNetworked(OneNet(nearIndex, 3, twinLow, twinHigh), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.EqInt(w.GetPendingSwapCount(), 1, "one swap pending");
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(b0.objects.Get(3) == null, "local copy deleted once the twin is present");
		DZOMS_T.EqInt(w.GetSwapsDone(), 1, "swap counted");
		DZOMS_T.EqInt(w.GetPendingSwapCount(), 0, "no swap pending");

		// a twin that is not where the entry says (placement problem): no swap, counted and logged
		Object off = g_Game.CreateObjectEx("Apple", base + "0.5 0 0.3", ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		int offLow, offHigh;
		off.GetNetworkID(offLow, offHigh);
		Object copy1 = b0.objects.Get(1);
		w.HandleNetworked(OneNet(nearIndex, 1, offLow, offHigh), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(b0.objects.Get(1) == copy1, "a twin 0.3 m off does not replace the local copy");
		DZOMS_T.Check(w.GetTwinMismatchCount() >= 1, "the displaced twin is counted");
		w.HandleLocal(One(nearIndex, 1), DZOMS_Test_ClientSpawn.s_Time);
		g_Game.ObjectDelete(off);

		// announced without a networked instance: after the timeout the local copy stays
		Object copy4 = b0.objects.Get(4);
		w.HandleNetworked(OneNet(nearIndex, 4, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.EqInt(w.GetPendingSwapCount(), 1, "no twin: still pending");
		DZOMS_T.Check(b0.objects.Get(4) == copy4, "no twin: local copy kept");
		DZOMS_Test_ClientSpawn.Frame(w, base, 10500);
		DZOMS_T.EqInt(w.GetPendingSwapCount(), 0, "timeout drops the pending swap");
		DZOMS_T.EqInt(w.GetSwapTimeoutCount(), 1, "timeout counted");
		DZOMS_T.Check(b0.objects.Get(4) == copy4, "timeout: local copy kept");

		// back to local: the local copy returns
		w.HandleLocal(One(nearIndex, 3), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.Check(!w.IsAnnouncedNetworked(nearIndex, 3), "id removed from the set");
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(b0.objects.Get(3) != null, "local copy is back");
		g_Game.ObjectDelete(twin);

		// announcements without network ids are ignored
		int idsBefore = w.GetNetworkedIdCount();
		w.HandleNetworked(One(nearIndex, 1), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.EqInt(w.GetNetworkedIdCount(), idsBefore, "an announcement without network ids is ignored");

		// ids that are invalid or unknown change nothing and do not throw
		int before = w.GetNetworkedIdCount();
		w.HandleNetworked(OneNet(99, 0, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleNetworked(OneNet(nearIndex, 500, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleNetworked(OneNet(-1, 0, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleNetworked(OneNet(nearIndex, -3, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleLocal(One(nearIndex, 5), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleLocal(One(99, 99), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_IdList skewed = new DZOMS_IdList();
		skewed.batch.Insert(nearIndex);
		w.HandleNetworked(skewed, DZOMS_Test_ClientSpawn.s_Time);
		w.HandleNetworked(null, DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.EqInt(w.GetNetworkedIdCount(), before, "invalid ids are ignored");
		// duplicates: the second announcement changes nothing
		w.HandleNetworked(OneNet(nearIndex, 2, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_T.EqInt(w.GetNetworkedIdCount(), before, "duplicate announcement ignored");
		// the same id in both messages of one tick: the later one wins
		w.HandleNetworked(OneNet(nearIndex, 5, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		w.HandleLocal(One(nearIndex, 5), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(!w.IsAnnouncedNetworked(nearIndex, 5), "local after networked: not announced");
		DZOMS_T.Check(b0.objects.Get(5) != null, "local after networked: local copy exists");

		// unload and reload honour the set
		DZOMS_Test_ClientSpawn.Frame(w, base + "6000 0 0", 300);
		DZOMS_Test_ClientSpawn.Frame(w, base + "6000 0 0", 16);
		DZOMS_T.EqInt(DZOMS_Test_ClientSpawn.Live(b0), 0, "far away: everything unloaded");
		DZOMS_T.Check(w.IsAnnouncedNetworked(nearIndex, 2), "the set survives the unload");
		DZOMS_Test_ClientSpawn.Frame(w, base, 300);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_Test_ClientSpawn.Frame(w, base, 16);
		DZOMS_T.Check(b0.objects.Get(2) == null, "reloaded batch skips the announced entry");
		DZOMS_T.Check(b0.objects.Get(0) != null, "reloaded batch spawns the others");
		w.Destroy();

		// the cap on the set
		DZOMS_ClientWorld capped = MakeClient(base + "50 0 0");
		capped.m_MaxAnnounced = 3;
		for (int c = 0; c < 6; c++)
		{
			capped.HandleNetworked(OneNet(nearIndex, c, 7000, 0), DZOMS_Test_ClientSpawn.s_Time);
		}
		DZOMS_T.EqInt(capped.GetNetworkedIdCount(), 3, "the set stops at the cap");
		DZOMS_T.EqInt(capped.GetAnnounceDropped(), 3, "ids over the cap are counted");
		capped.Destroy();
	}

	static DZOMS_PlayerState Player(int id, vector pos, bool alive)
	{
		DZOMS_PlayerState p = new DZOMS_PlayerState();
		p.id = id;
		p.pos = pos;
		p.alive = alive;
		return p;
	}

	static array<ref DZOMS_PlayerState> Players(DZOMS_PlayerState a, DZOMS_PlayerState b)
	{
		array<ref DZOMS_PlayerState> list = new array<ref DZOMS_PlayerState>();
		if (a)
		{
			list.Insert(a);
		}
		if (b)
		{
			list.Insert(b);
		}
		return list;
	}

	// objects of a type at a position (5 cm), and how many of them have a network id
	static int CountAt(string type, vector pos, out int networked)
	{
		array<Object> found = new array<Object>();
		g_Game.GetObjectsAtPosition3D(pos, 0.3, found, null);
		int n = 0;
		networked = 0;
		for (int i = 0; i < found.Count(); i++)
		{
			Object o = found.Get(i);
			if (!o || o.IsPendingDeletion() || o.GetType() != type || vector.Distance(o.GetPosition(), pos) > 0.05)
			{
				continue;
			}
			n++;
			int low, high;
			o.GetNetworkID(low, high);
			if (low != 0 || high != 0)
			{
				networked++;
			}
		}
		return n;
	}

	static int PromotedAmong(DZOMS_ServerWorld w, int from, int to)
	{
		int n = 0;
		for (int i = from; i <= to; i++)
		{
			if (w.IsPromoted(i))
			{
				n++;
			}
		}
		return n;
	}

	static int AnnouncedIds(DZOMS_ServerWorld w, int rpcType)
	{
		int n = 0;
		for (int i = 0; i < w.m_BroadcastTypes.Count(); i++)
		{
			if (w.m_BroadcastTypes.Get(i) == rpcType)
			{
				n += w.m_BroadcastLists.Get(i).Count();
			}
		}
		return n;
	}

	static void RunPromotion()
	{
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -250";
		float r = DZOMS_Proximity.NETWORK_RADIUS;
		string mapsDir = DZOMS_T.DIR + "/DZE_prox_promo";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(mapsDir);
		EditorSaveData data = new EditorSaveData();
		for (int i = 0; i < 10; i++)
		{
			DZOMS_TestFixtures.AddObject(data, "Apple", base + Vector(i * r / 10, 0, 0), "0 0 0", 1, 0);
		}
		DZOMS_TestFixtures.AddObject(data, "Apple", base + Vector(0, 0, r + 1), "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(data, "Apple", base + Vector(r * 2.5, 0, 0), "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(data, "Land_House_1W10", base + Vector(0, 0, r * 0.4), "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(data, "DZOMS_No_Such_Class", base + "5 0 5", "0 0 0", 1, 0);
		DZOMS_TestFixtures.Save(data, mapsDir + "/promo.dze");
		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		DZOMS_MapEntry e = new DZOMS_MapEntry();
		e.file = "promo.dze";
		c.maps.Insert(e);
		c.networked = {"Land_House_1W10"};
		c.Normalize();
		DZOMS_ServerWorld w = new DZOMS_ServerWorld(c, mapsDir, new DZOMS_ObjectProbe());
		w.m_Offline = true;
		w.Load();
		DZOMS_T.EqInt(w.GetObjectCount(), 14, "promotion world: 14 objects");
		string digest = w.GetDigest();
		string hashes = "";
		for (int h = 0; h < w.GetManifest().hashes.Count(); h++)
		{
			hashes += w.GetManifest().hashes.Get(h);
		}
		Object houseBefore = w.GetSpawned().Get(12);
		DZOMS_T.Check(houseBefore != null, "rule networked house exists");
		DZOMS_T.Check(w.GetSpawned().Get(13) == null, "missing class has no object");

		DZOMS_Proximity prox = new DZOMS_Proximity();
		prox.Init(w);
		DZOMS_PlayerState a = Player(1, base, false);

		// a dead player has no zone
		prox.Update(Players(a, null), 0);
		prox.ProcessQueue(0);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "dead player: nothing promoted");

		// alive: nearest first, at most CREATES_PER_FRAME per call
		a.alive = true;
		prox.Update(Players(a, null), 1000);
		prox.ProcessQueue(1000);
		DZOMS_T.EqInt(w.GetPromotedCount(), DZOMS_Proximity.CREATES_PER_FRAME, "first call promotes the per-frame budget");
		DZOMS_T.EqInt(PromotedAmong(w, 0, 4), 5, "the 5 nearest objects went first");
		int nets;
		DZOMS_T.EqInt(CountAt("Apple", base, nets), 1, "promoted object: one copy at its position");
		DZOMS_T.EqInt(nets, 1, "promoted object: the copy is networked");
		DZOMS_T.EqInt(CountAt("Apple", base + Vector(r * 0.5, 0, 0), nets), 1, "not yet promoted: one copy");
		DZOMS_T.EqInt(nets, 0, "not yet promoted: still local");
		prox.ProcessQueue(1001);
		DZOMS_T.EqInt(w.GetPromotedCount(), 10, "second call promotes the rest");
		DZOMS_T.Check(!w.IsPromoted(10) && !w.IsPromoted(11), "outside the radius and far objects stay local");
		DZOMS_T.Check(!w.IsPromoted(12) && w.GetSpawned().Get(12) == houseBefore, "rule networked object untouched");
		DZOMS_T.Check(!w.IsPromoted(13), "failed object untouched");
		DZOMS_T.EqInt(AnnouncedIds(w, DZOMS_Rpc.OBJECTS_NETWORKED), 0, "no announcement before the swap delay");
		prox.ProcessQueue(1600);
		DZOMS_T.EqInt(AnnouncedIds(w, DZOMS_Rpc.OBJECTS_NETWORKED), 10, "all 10 announced after the swap delay");
		int idBad = 0;
		for (int al = 0; al < w.m_BroadcastLists.Count(); al++)
		{
			DZOMS_IdList sent = w.m_BroadcastLists.Get(al);
			if (w.m_BroadcastTypes.Get(al) != DZOMS_Rpc.OBJECTS_NETWORKED)
			{
				continue;
			}
			if (!sent.HasNetIds())
			{
				idBad++;
				continue;
			}
			for (int an = 0; an < sent.Count(); an++)
			{
				Object resolved = g_Game.GetObjectByNetworkId(sent.netLow.Get(an), sent.netHigh.Get(an));
				if (!resolved || resolved.GetType() != "Apple")
				{
					idBad++;
				}
			}
		}
		DZOMS_T.EqInt(idBad, 0, "announced network ids resolve to the networked copies");
		DZOMS_T.EqInt(prox.GetNetworkedCount(), 10, "counter: 10 networked");
		DZOMS_T.EqInt(prox.GetPromotions(), 10, "counter: 10 promotions");

		// the interval: an update 500 ms later does nothing, a zone that moved away needs the release delay
		a.pos = base + Vector(0, 0, r * 12);
		prox.Update(Players(a, null), 1500);
		prox.ProcessQueue(1600);
		DZOMS_T.EqInt(w.GetPromotedCount(), 10, "update inside the interval changes nothing");
		prox.Update(Players(a, null), 2500);
		prox.ProcessQueue(2500);
		DZOMS_T.EqInt(w.GetPromotedCount(), 10, "nothing is released before the release delay");

		// release: local copy first, networked copy deleted after the swap delay
		prox.Update(Players(a, null), 32000);
		prox.ProcessQueue(32000);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "released after the release delay");
		DZOMS_T.EqInt(CountAt("Apple", base, nets), 2, "right after the demotion both copies exist");
		DZOMS_T.EqInt(nets, 1, "right after the demotion one of them is networked");
		DZOMS_T.EqInt(AnnouncedIds(w, DZOMS_Rpc.OBJECTS_LOCAL), 10, "the demotion was announced");
		prox.ProcessQueue(32600);
		DZOMS_T.EqInt(CountAt("Apple", base, nets), 1, "after the swap delay one copy is left");
		DZOMS_T.EqInt(nets, 0, "and it is local");
		DZOMS_T.EqInt(prox.GetDemotions(), 10, "counter: 10 demotions");

		// two players: objects near the second one stay networked
		a.pos = base;
		prox.Update(Players(a, null), 33000);
		prox.ProcessQueue(33000);
		prox.ProcessQueue(33001);
		string which = "";
		for (int pw = 0; pw < w.GetObjectCount(); pw++)
		{
			if (w.IsPromoted(pw))
			{
				which += pw.ToString() + " ";
			}
		}
		DZOMS_T.EqInt(w.GetPromotedCount(), 10, "promoted again: " + which);
		a.pos = base + Vector(0, 0, r * 12);
		DZOMS_PlayerState b = Player(2, base + Vector(r * 1.75, 0, 0), true);
		prox.Update(Players(a, b), 34000);
		prox.ProcessQueue(34000);
		prox.Update(Players(a, b), 65000);
		prox.ProcessQueue(65000);
		DZOMS_T.EqInt(PromotedAmong(w, 0, 2), 0, "more than radius + hysteresis from both players: released");
		DZOMS_T.EqInt(PromotedAmong(w, 3, 7), 5, "inside the hysteresis band of the second player: kept");
		DZOMS_T.EqInt(PromotedAmong(w, 8, 9), 2, "inside the second player's zone: kept");
		prox.ProcessQueue(65600);

		// a failed local respawn keeps the networked copy
		a.pos = base;
		b.alive = false;
		prox.Update(Players(a, b), 66000);
		prox.ProcessQueue(66000);
		prox.ProcessQueue(66001);
		// 10 near the first player, plus the apple 100 m away that the second player's zone promoted
		DZOMS_T.EqInt(w.GetPromotedCount(), 11, "promoted a third time");
		w.GetObjects().Get(3).type = "DZOMS_No_Such_Class";
		a.pos = base + Vector(0, 0, r * 12);
		prox.Update(Players(a, b), 97000);
		prox.ProcessQueue(97000);
		prox.ProcessQueue(97001);
		DZOMS_T.Check(w.IsPromoted(3), "failed local respawn: still promoted");
		DZOMS_T.EqInt(CountAt("Apple", base + Vector(r * 0.3, 0, 0), nets), 1, "failed local respawn: the networked copy is kept");
		DZOMS_T.EqInt(nets, 1, "failed local respawn: and it is networked");
		DZOMS_T.EqInt(w.GetPromotedCount(), 1, "failed local respawn: the others were released");
		DZOMS_T.Check(prox.GetFailures() >= 1, "failed local respawn is counted");

		// the status line: short, only when something happened, not twice within 10 s
		string line = prox.StatusLine(100000);
		DZOMS_T.Check(line != "" && line.Length() < 200, "status line after activity, short: " + line);
		DZOMS_T.Eq(prox.StatusLine(100001), "", "no second status line within 10 s");
		DZOMS_T.Eq(prox.StatusLine(120000), "", "no status line without activity");

		// promotion and demotion do not touch the data
		DZOMS_T.Eq(w.GetDigest(), digest, "digest unchanged");
		string hashesAfter = "";
		for (int k = 0; k < w.GetManifest().hashes.Count(); k++)
		{
			hashesAfter += w.GetManifest().hashes.Get(k);
		}
		DZOMS_T.Eq(hashesAfter, hashes, "batch hashes unchanged");
		w.Destroy();
	}

	// experimentalKeepLocalCopy: promotion keeps the server's local copy, demotion reuses it
	static void RunKeepLocal()
	{
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -180";
		DZOMS_ServerWorld w = MakeWorld("prox_keep", 3, base);
		w.GetConfig().experimentalKeepLocalCopy = true;
		w.GetConfig().experimentalNoPathgraph = true;
		DZOMS_Proximity prox = new DZOMS_Proximity();
		prox.Init(w);
		Object localBefore = w.GetSpawned().Get(1);
		DZOMS_PlayerState a = Player(1, base, true);
		prox.Update(Players(a, null), 0);
		prox.ProcessQueue(0);
		DZOMS_T.EqInt(w.GetPromotedCount(), 3, "keep local: 3 promoted");
		int nets;
		DZOMS_T.EqInt(CountAt("Apple", base + "10 0 0", nets), 2, "keep local: both copies exist on the server");
		DZOMS_T.EqInt(nets, 1, "keep local: one of them is networked");
		DZOMS_T.Check(w.GetSpawned().Get(1) != localBefore, "keep local: the slot is the networked copy");

		// demotion reuses the kept local copy: no spawn, so it cannot fail
		w.GetObjects().Get(1).type = "DZOMS_No_Such_Class";
		a.pos = base + Vector(0, 0, 10000);
		prox.Update(Players(a, null), 40000);
		prox.ProcessQueue(40000);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "keep local: all demoted, also the one whose class cannot be spawned");
		DZOMS_T.Check(w.GetSpawned().Get(1) == localBefore, "keep local: demotion put the original local copy back");
		DZOMS_T.EqInt(prox.GetFailures(), 0, "keep local: no failures");
		prox.ProcessQueue(40600);
		DZOMS_T.EqInt(CountAt("Apple", base + "10 0 0", nets), 1, "keep local: one copy left after the swap delay");
		DZOMS_T.EqInt(nets, 0, "keep local: and it is local");
		w.Destroy();
	}

	static void Run()
	{
		DZOMS_T.Suite("proximity");
		RunKeepLocal();
		RunPromotion();
		RunProtocol();
		RunServerState();
		RunClientSwap();
	}
}
