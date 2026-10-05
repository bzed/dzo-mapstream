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

		// a twin shows up with scale 1 (the scale is not replicated): the client gives it the entry's scale
		array<ref DZOMS_ObjectEntry> sl = new array<ref DZOMS_ObjectEntry>();
		vector sbase = base + "100 0 0";
		sl.Insert(new DZOMS_ObjectEntry("Apple", sbase, "0 0 0", 2, 0));
		array<ref DZOMS_Batch> ssb = new array<ref DZOMS_Batch>();
		array<float> sbo = new array<float>();
		DZOMS_BatchBuilder.Build(sl, 400, ssb, sbo);
		DZOMS_ClientWorld sw = new DZOMS_ClientWorld("");
		sw.m_Offline = true;
		sw.Update(sbase, DZOMS_Test_ClientSpawn.s_Time, true);
		sw.HandleManifest(DZOMS_Test_ClientDownload.MakeManifest(ssb, sbo, true), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Batch sc = DZOMS_Test_ClientDownload.Clone(ssb.Get(0));
		sc.index = 0;
		sw.HandleBatch(sc, DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(sw, sbase, 16);
		DZOMS_Test_ClientSpawn.Frame(sw, sbase, 16);
		Object scaledCopy = sw.GetClientBatch(0).objects.Get(0);
		Object scaledTwin = g_Game.CreateObjectEx("Apple", sbase, ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		int stLow, stHigh;
		scaledTwin.GetNetworkID(stLow, stHigh);
		sw.HandleNetworked(OneNet(0, 0, stLow, stHigh), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(sw, sbase, 16);
		DZOMS_T.Check(sw.GetClientBatch(0).objects.Get(0) == null, "the twin took over after it was given the entry's scale");
		DZOMS_T.EqFloat(scaledTwin.GetScale(), 2, "the twin has the entry's scale now");
		DZOMS_T.EqInt(sw.GetScaleFixedCount(), 1, "the scaled twin is counted");
		g_Game.ObjectDelete(scaledTwin);
		sw.Destroy();

		// login in an area whose objects are networked already: announced before the batch spawns, no
		// local copy exists, and the twin that streams in still gets its scale (and keeps it)
		array<ref DZOMS_ObjectEntry> ll = new array<ref DZOMS_ObjectEntry>();
		vector lbase = base + "200 0 0";
		ll.Insert(new DZOMS_ObjectEntry("Apple", lbase, "0 0 0", 2, 0));
		array<ref DZOMS_Batch> lsb = new array<ref DZOMS_Batch>();
		array<float> lbo = new array<float>();
		DZOMS_BatchBuilder.Build(ll, 400, lsb, lbo);
		DZOMS_ClientWorld lw = new DZOMS_ClientWorld("");
		lw.m_Offline = true;
		lw.Update(lbase, DZOMS_Test_ClientSpawn.s_Time, true);
		lw.HandleManifest(DZOMS_Test_ClientDownload.MakeManifest(lsb, lbo, true), DZOMS_Test_ClientSpawn.s_Time);
		Object loginTwin = g_Game.CreateObjectEx("Apple", lbase, ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		int ltLow, ltHigh;
		loginTwin.GetNetworkID(ltLow, ltHigh);
		lw.HandleNetworked(OneNet(0, 0, ltLow, ltHigh), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Batch lc = DZOMS_Test_ClientDownload.Clone(lsb.Get(0));
		lc.index = 0;
		lw.HandleBatch(lc, DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(lw, lbase, 16);
		DZOMS_Test_ClientSpawn.Frame(lw, lbase, 16);
		DZOMS_T.Check(lw.GetClientBatch(0).objects.Get(0) == null, "login: no local copy of an announced entry");
		DZOMS_T.EqFloat(loginTwin.GetScale(), 2, "login: the streamed twin got the entry's scale");
		loginTwin.SetScale(1);
		DZOMS_Test_ClientSpawn.Frame(lw, lbase, 3000);
		DZOMS_T.EqFloat(loginTwin.GetScale(), 2, "login: a twin that lost its scale gets it again");
		lw.HandleLocal(One(0, 0), DZOMS_Test_ClientSpawn.s_Time);
		loginTwin.SetScale(1);
		DZOMS_Test_ClientSpawn.Frame(lw, lbase, 3000);
		DZOMS_T.EqFloat(loginTwin.GetScale(), 1, "an entry that is local again is no longer watched");
		g_Game.ObjectDelete(loginTwin);
		lw.Destroy();

		// a networked model twin is created by the server's engine off its entry (bounding box centred,
		// orientation not applied) and the position is not corrected on the client: the client puts it right
		string modelPath = "dz\\structures\\industrial\\containers\\container_1bo.p3d";
		vector mbase = base + "300 0 0";
		Object modelTwin = g_Game.CreateStaticObjectUsingP3D(modelPath, mbase, "0.7 0 0", 1, false);
		vector twinOrigin = modelTwin.GetPosition();
		vector twinOri = modelTwin.GetOrientation();
		array<ref DZOMS_ObjectEntry> ml = new array<ref DZOMS_ObjectEntry>();
		ml.Insert(new DZOMS_ObjectEntry(modelPath, twinOrigin, twinOri, 2, DZOMS_Batch.FLAG_ORIGIN));
		array<ref DZOMS_Batch> msb = new array<ref DZOMS_Batch>();
		array<float> mbo = new array<float>();
		DZOMS_BatchBuilder.Build(ml, 400, msb, mbo);
		DZOMS_ClientWorld mw = new DZOMS_ClientWorld("");
		mw.m_Offline = true;
		mw.Update(mbase, DZOMS_Test_ClientSpawn.s_Time, true);
		mw.HandleManifest(DZOMS_Test_ClientDownload.MakeManifest(msb, mbo, true), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Batch mc = DZOMS_Test_ClientDownload.Clone(msb.Get(0));
		mc.index = 0;
		mw.HandleBatch(mc, DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(mw, mbase, 16);
		DZOMS_Test_ClientSpawn.Frame(mw, mbase, 16);
		Object modelCopy = mw.GetClientBatch(0).objects.Get(0);
		DZOMS_T.Check(modelCopy != null, "model entry: local copy exists");
		DZOMS_T.EqFloat(modelTwin.GetScale(), 1, "the model twin shows up unscaled (scale is not replicated)");
		int mtLow, mtHigh;
		modelTwin.GetNetworkID(mtLow, mtHigh);
		mw.HandleNetworked(OneNet(0, 0, mtLow, mtHigh), DZOMS_Test_ClientSpawn.s_Time);
		DZOMS_Test_ClientSpawn.Frame(mw, mbase, 16);
		DZOMS_T.Check(mw.GetClientBatch(0).objects.Get(0) == null, "model entry: the local copy gave way to the twin");
		DZOMS_T.EqFloat(vector.Distance(modelTwin.GetPosition(), twinOrigin), 0, "model twin stays at its origin");
		DZOMS_T.EqFloat(modelTwin.GetOrientation()[0], twinOri[0], "model twin keeps its yaw");
		DZOMS_T.EqFloat(modelTwin.GetScale(), 2, "model twin: scale as in the entry");
		g_Game.ObjectDelete(modelTwin);
		mw.Destroy();

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
		float r = DZOMS_Config.DEFAULT_NETWORK_RADIUS;
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
		prox.m_ActivateDelayMs = 0;
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
		int failuresBefore = prox.GetFailures();
		prox.Update(Players(a, b), 98000);
		prox.ProcessQueue(98000);
		DZOMS_T.EqInt(prox.GetFailures(), failuresBefore, "failed local respawn: not retried within a minute");
		DZOMS_T.Check(w.IsPromoted(3), "failed local respawn: still promoted after the next update");
		prox.Update(Players(a, b), 158000);
		prox.ProcessQueue(158000);
		DZOMS_T.EqInt(prox.GetFailures(), failuresBefore + 1, "failed local respawn: retried after a minute");

		// the status line: short, only when something happened, not twice within 10 s
		string line = prox.StatusLine(100000);
		DZOMS_T.Check(line != "" && line.Length() < 200, "status line after activity, short: " + line);
		DZOMS_T.Eq(prox.StatusLine(100001), "", "no second status line within 10 s");
		DZOMS_T.Eq(prox.StatusLine(130000), "", "no status line before a minute has passed");
		DZOMS_T.Eq(prox.StatusLine(170000), "", "no status line without activity");
		array<string> typeLines = prox.TypeReport(5);
		DZOMS_T.Check(typeLines.Count() >= 1 && typeLines.Get(0).Contains("apple") && typeLines.Get(0).Length() < 200, "type report names the promoted types");

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

	static DZOMS_PlayerState StateOf(int id, vector pos, float speed, bool vehicle, float height)
	{
		DZOMS_PlayerState p = new DZOMS_PlayerState();
		p.id = id;
		p.pos = pos;
		p.speed = speed;
		p.inVehicle = vehicle;
		p.height = height;
		p.alive = true;
		return p;
	}

	// zone rules of DESIGN 13.3
	static void RunZoneRules()
	{
		DZOMS_ProximityConfig cfg = new DZOMS_ProximityConfig();
		cfg.networkRadius = 150;
		cfg.vehicleRadius = 40;
		cfg.maxFootSpeed = 6;
		cfg.maxVehicleSpeed = 8;
		cfg.maxHeight = 20;
		cfg.maxNetworked = 6000;
		DZOMS_Zone z;

		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 1.5, false, 0), cfg);
		DZOMS_T.Check(z.active && z.radius == 150, "walking: zone of networkRadius");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 5, false, 0), cfg);
		DZOMS_T.Check(z.active, "jogging at 5 m/s: zone");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 6.1, false, 0), cfg);
		DZOMS_T.Check(!z.active, "faster than maxFootSpeed: no zone");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 2, true, 0), cfg);
		DZOMS_T.Check(z.active && z.radius == 40, "slow vehicle: zone of vehicleRadius");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 8.1, true, 0), cfg);
		DZOMS_T.Check(!z.active, "faster than maxVehicleSpeed: no zone");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 3, true, 50), cfg);
		DZOMS_T.Check(!z.active, "helicopter 50 m up: no zone whatever the speed");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 3, true, 15), cfg);
		DZOMS_T.Check(z.active && z.radius == 40, "slow vehicle 15 m up: zone");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 1, false, 19), cfg);
		DZOMS_T.Check(z.active, "on foot 19 m above the ground (a roof): zone");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 1, false, 21), cfg);
		DZOMS_T.Check(!z.active, "on foot 21 m above the ground: no zone");
		DZOMS_PlayerState dead = StateOf(1, "0 0 0", 0, false, 0);
		dead.alive = false;
		z = DZOMS_Proximity.ZoneFor(dead, cfg);
		DZOMS_T.Check(!z.active, "dead: no zone");
		cfg.networkRadius = 80;
		cfg.maxFootSpeed = 3;
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 4, false, 0), cfg);
		DZOMS_T.Check(!z.active, "custom maxFootSpeed");
		z = DZOMS_Proximity.ZoneFor(StateOf(1, "0 0 0", 2, false, 0), cfg);
		DZOMS_T.Check(z.active && z.radius == 80, "custom networkRadius");
	}

	// fast movers, vehicles, the activation delay, the exclusion list and the cap, on a real world
	static void RunProximityRules()
	{
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -350";
		DZOMS_ServerWorld w = MakeWorld("prox_rules", 10, base);
		DZOMS_Proximity prox = new DZOMS_Proximity();
		prox.Init(w);
		DZOMS_ProximityConfig cfg = prox.GetConfig();
		cfg.networkRadius = 50;
		cfg.vehicleRadius = 20;

		// a runner and a helicopter promote nothing
		prox.m_ActivateDelayMs = 0;
		prox.Update(Players(StateOf(1, base, 9, false, 0), null), 0);
		prox.ProcessQueue(0);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "a runner promotes nothing");
		prox.Update(Players(StateOf(1, base, 2, true, 80), null), 1000);
		prox.ProcessQueue(1000);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "a helicopter promotes nothing");
		prox.Update(Players(StateOf(1, base, 12, true, 0), null), 2000);
		prox.ProcessQueue(2000);
		DZOMS_T.EqInt(w.GetPromotedCount(), 0, "a fast car promotes nothing");

		// a slow vehicle: the smaller zone (objects every 10 m at x = 0..90)
		prox.Update(Players(StateOf(1, base, 2, true, 0), null), 3000);
		prox.ProcessQueue(3000);
		prox.ProcessQueue(3001);
		DZOMS_T.EqInt(w.GetPromotedCount(), 3, "slow vehicle: objects within 20 m (x = 0, 10, 20)");

		// on foot: the larger zone
		prox.Update(Players(StateOf(1, base, 1, false, 0), null), 4000);
		prox.ProcessQueue(4000);
		prox.ProcessQueue(4001);
		DZOMS_T.EqInt(w.GetPromotedCount(), 6, "on foot: objects within 50 m (x = 0 .. 50)");

		// the activation delay: 2 s of qualifying before the zone opens
		DZOMS_ServerWorld w2 = MakeWorld("prox_delay", 4, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -420");
		DZOMS_Proximity delayed = new DZOMS_Proximity();
		delayed.Init(w2);
		vector base2 = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -420";
		delayed.Update(Players(StateOf(7, base2, 1, false, 0), null), 0);
		delayed.ProcessQueue(0);
		DZOMS_T.EqInt(w2.GetPromotedCount(), 0, "delay: nothing at the first sight of the player");
		delayed.Update(Players(StateOf(7, base2, 1, false, 0), null), 1000);
		delayed.ProcessQueue(1000);
		DZOMS_T.EqInt(w2.GetPromotedCount(), 0, "delay: nothing after 1 s");
		delayed.Update(Players(StateOf(7, base2, 8, false, 0), null), 2000);
		delayed.Update(Players(StateOf(7, base2, 1, false, 0), null), 3000);
		delayed.ProcessQueue(3000);
		DZOMS_T.EqInt(w2.GetPromotedCount(), 0, "delay: a player who ran in between starts again");
		delayed.Update(Players(StateOf(7, base2, 1, false, 0), null), 5000);
		delayed.ProcessQueue(5000);
		DZOMS_T.Check(w2.GetPromotedCount() > 0, "delay: promoted after 2 s of qualifying");
		w2.Destroy();

		// cap: 10 candidates around the player, room for 4
		DZOMS_ServerWorld w3 = MakeWorld("prox_cap", 10, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -490");
		vector base3 = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -490";
		DZOMS_Proximity capped = new DZOMS_Proximity();
		capped.Init(w3);
		capped.m_ActivateDelayMs = 0;
		capped.GetConfig().maxNetworked = 4;
		capped.Update(Players(StateOf(1, base3, 1, false, 0), null), 0);
		capped.ProcessQueue(0);
		capped.ProcessQueue(1);
		DZOMS_T.EqInt(w3.GetPromotedCount(), 4, "cap: at most maxNetworked objects promoted");
		DZOMS_T.EqInt(PromotedAmong(w3, 0, 3), 4, "cap: the nearest objects win");
		DZOMS_T.Check(capped.GetCapHits() >= 1, "cap: counted");
		w3.Destroy();

		// a promotion that fails is not retried for a minute (Review Focus 3 and 4 of the plan)
		DZOMS_ServerWorld w5 = MakeWorld("prox_retry", 2, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -630");
		vector base5 = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -630";
		w5.GetObjects().Get(0).type = "DZOMS_No_Such_Class";
		DZOMS_Proximity retry = new DZOMS_Proximity();
		retry.Init(w5);
		retry.m_ActivateDelayMs = 0;
		retry.Update(Players(StateOf(1, base5, 1, false, 0), null), 0);
		retry.ProcessQueue(0);
		DZOMS_T.EqInt(retry.GetFailures(), 1, "retry: the failed promotion is counted");
		DZOMS_T.Check(w5.IsPromoted(1) && !w5.IsPromoted(0), "retry: the other object was promoted");
		retry.Update(Players(StateOf(1, base5, 1, false, 0), null), 2000);
		retry.ProcessQueue(2000);
		DZOMS_T.EqInt(retry.GetFailures(), 1, "retry: no new attempt within a minute");
		retry.Update(Players(StateOf(1, base5, 1, false, 0), null), 62000);
		retry.ProcessQueue(62000);
		DZOMS_T.EqInt(retry.GetFailures(), 2, "retry: tried again after a minute");
		w5.Destroy();

		// the exclusion list: a type that is never promoted
		DZOMS_ServerWorld w4 = MakeWorld("prox_excl", 3, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -560");
		w4.GetConfig().proximityExclude = {"app*"};
		DZOMS_Proximity excl = new DZOMS_Proximity();
		excl.Init(w4);
		excl.m_ActivateDelayMs = 0;
		excl.Update(Players(StateOf(1, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -560", 1, false, 0), null), 0);
		excl.ProcessQueue(0);
		DZOMS_T.EqInt(w4.GetPromotedCount(), 0, "excluded types are never promoted (glob, any case)");
		w4.Destroy();
		w.Destroy();
	}

	static void RunDisabled()
	{
		DZOMS_ServerWorld w = MakeWorld("prox_off", 2, DZOMS_TestUtil.TEST_ORIGIN + "0 0 -120");
		w.GetConfig().disableProximity = true;
		w.StartProximity();
		DZOMS_T.Check(w.GetProximity() == null, "disableProximity: no proximity object");
		w.GetConfig().disableProximity = false;
		w.StartProximity();
		DZOMS_T.Check(w.GetProximity() != null, "enabled: proximity object exists");
		w.Destroy();
	}

	// model objects. A model entry of a map file holds the position the Editor passed to
	// CreateStaticObjectUsingP3D; the object's origin ends up elsewhere (bottom of its bounding box on
	// that position, DESIGN 13.9) and the Editor shows that origin. Entries with FLAG_ORIGIN hold the
	// origin and degrees (what the server rewrites them to after spawning); they are placed exactly.
	static void RunModelPlacement()
	{
		array<string> models = {"dz\\plants_bliss\\bush\\b_caraganaarborescens_2s_summer.p3d", "dz\\plants_bliss\\clutter\\c_ferntall_summer.p3d", "dz\\structures\\industrial\\containers\\container_1bo.p3d"};
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -90";
		for (int m = 0; m < models.Count(); m++)
		{
			for (int v = 0; v < 4; v++)
			{
				vector pos = at + Vector(m * 30 + v * 8, 0, 0);
				float scale = 1;
				if (m == 2)
				{
					scale = 2;
				}
				bool networked = v % 2 == 1;
				string what = "model " + m.ToString() + " networked=" + networked.ToString();
				Object o;
				if (v < 2)
				{
					// as a map file gives it: radians, the position the engine puts the base on
					what = what + " base entry";
					o = DZOMS_ServerWorld.SpawnOnServer(new DZOMS_ObjectEntry(models.Get(m), pos, "0.7 0 0", scale, 0), networked);
					DZOMS_T.Check(o != null, what + ": created");
					if (o)
					{
						DZOMS_T.EqFloat(o.GetOrientation()[0], 0.7 * Math.RAD2DEG, what + ": yaw from radians");
						DZOMS_T.EqFloat(o.GetScale(), scale, what + ": scale");
					}
				}
				else
				{
					what = what + " origin entry";
					o = DZOMS_ServerWorld.SpawnOnServer(new DZOMS_ObjectEntry(models.Get(m), pos, "40 0 0", scale, DZOMS_Batch.FLAG_ORIGIN), networked);
					DZOMS_T.Check(o != null, what + ": created");
					if (o)
					{
						DZOMS_T.EqFloat(vector.Distance(o.GetPosition(), pos), 0, what + ": exactly at the entry");
						DZOMS_T.EqFloat(o.GetOrientation()[0], 40, what + ": yaw in degrees");
						DZOMS_T.EqFloat(o.GetScale(), scale, what + ": scale");
					}
				}
				if (o)
				{
					g_Game.ObjectDelete(o);
				}
			}
		}
	}

	// a map file with a model object: the server rewrites the entry to the origin the engine gave the object
	static void RunModelEntries()
	{
		string mapsDir = DZOMS_T.DIR + "/DZE_prox_models";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(mapsDir);
		string crate = "dz\\structures_bliss\\residential\\misc\\misc_woodencrate_3x.p3d";
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -70";
		EditorSaveData data = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(data, crate, at, "0.314159 0 0", 1, 0);
		DZOMS_TestFixtures.Save(data, mapsDir + "/models.dze");
		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		DZOMS_MapEntry me = new DZOMS_MapEntry();
		me.file = "models.dze";
		c.maps.Insert(me);
		c.Normalize();
		DZOMS_ServerWorld w = new DZOMS_ServerWorld(c, mapsDir, new DZOMS_ObjectProbe());
		w.m_Offline = true;
		w.Load();
		DZOMS_T.EqInt(w.GetObjectCount(), 1, "model map: one object");
		if (w.GetObjectCount() == 1 && w.GetSpawned().Get(0))
		{
			DZOMS_ObjectEntry e = w.GetObjects().Get(0);
			Object o = w.GetSpawned().Get(0);
			DZOMS_T.Check((e.flags & DZOMS_Batch.FLAG_ORIGIN) == DZOMS_Batch.FLAG_ORIGIN, "model entry is marked as origin data");
			DZOMS_T.EqFloat(vector.Distance(e.pos, o.GetPosition()), 0, "model entry holds the object's origin");
			DZOMS_T.EqFloat(e.ori[0], 18, "model entry holds the yaw in degrees");
			DZOMS_T.Check(vector.Distance(e.pos, at) > 0.1, "the origin is not the file's position (the engine puts the base there)");
			DZOMS_Batch b = w.GetBatches().Get(0);
			DZOMS_T.Check(b.flags.Get(0) == DZOMS_Batch.FLAG_ORIGIN, "the batch carries the origin flag");
			// the client places the local copy at that origin
			Object copy = DZOMS_ClientWorld.SpawnLocal(b.GetType(0), b.positions.Get(0), b.orientations.Get(0), b.scales.Get(0), b.flags.Get(0));
			DZOMS_T.Check(copy != null, "client copy created");
			if (copy)
			{
				DZOMS_T.EqFloat(vector.Distance(copy.GetPosition(), o.GetPosition()), 0, "client copy at the server object's origin");
				DZOMS_T.EqFloat(copy.GetOrientation()[0], 18, "client copy yaw");
				g_Game.ObjectDelete(copy);
			}
		}
		w.Destroy();
	}

	// the client's local model objects land where the entry says, too
	static void RunClientModelPlacement()
	{
		array<string> models = {"dz\\plants_bliss\\bush\\b_caraganaarborescens_2s_summer.p3d", "dz\\structures\\industrial\\containers\\container_1bo.p3d"};
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -60";
		for (int m = 0; m < models.Count(); m++)
		{
			vector pos = at + Vector(m * 30, 0, 0);
			Object o = DZOMS_ClientWorld.SpawnLocal(models.Get(m), pos, "40 0 0", 2, DZOMS_Batch.FLAG_ORIGIN);
			string what = "client model " + m.ToString();
			DZOMS_T.Check(o != null, what + ": created");
			if (!o)
			{
				continue;
			}
			DZOMS_T.EqFloat(vector.Distance(o.GetPosition(), pos), 0, what + ": at the entry position");
			DZOMS_T.EqFloat(o.GetOrientation()[0], 40, what + ": yaw in degrees");
			DZOMS_T.EqFloat(o.GetScale(), 2, what + ": scale as in the entry");
			g_Game.ObjectDelete(o);
		}
	}

	// scaled class objects keep position, yaw and scale (server and client)
	static void RunClassPlacement()
	{
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -30";
		for (int v = 0; v < 3; v++)
		{
			vector pos = at + Vector(v * 30, 0, 0);
			Object o;
			string what = "class placement " + v.ToString();
			if (v == 0)
			{
				o = DZOMS_ServerWorld.SpawnOnServer(new DZOMS_ObjectEntry("Land_Container_1Bo", pos, "40 0 0", 2, 0), false);
			}
			if (v == 1)
			{
				o = DZOMS_ServerWorld.SpawnOnServer(new DZOMS_ObjectEntry("Land_Container_1Bo", pos, "40 0 0", 2, 0), true);
			}
			if (v == 2)
			{
				o = DZOMS_ClientWorld.SpawnLocal("Land_Container_1Bo", pos, "40 0 0", 2, 0);
			}
			DZOMS_T.Check(o != null, what + ": created");
			if (!o)
			{
				continue;
			}
			DZOMS_T.EqFloat(vector.Distance(o.GetPosition(), pos), 0, what + ": position");
			DZOMS_T.EqFloat(o.GetOrientation()[0], 40, what + ": yaw");
			DZOMS_T.EqFloat(o.GetScale(), 2, what + ": scale");
			g_Game.ObjectDelete(o);
		}
	}

	static void Run()
	{
		DZOMS_T.Suite("proximity");
		RunClientModelPlacement();
		RunClassPlacement();
		RunModelPlacement();
		RunModelEntries();
		RunZoneRules();
		RunProximityRules();
		RunDisabled();
		RunPromotion();
		RunProtocol();
		RunServerState();
		RunClientSwap();
	}
}
