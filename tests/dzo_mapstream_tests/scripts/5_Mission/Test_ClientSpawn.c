class DZOMS_Test_ClientSpawn
{
	static int s_Time;

	static int Live(DZOMS_ClientBatch cb)
	{
		int n = 0;
		for (int i = 0; i < cb.objects.Count(); i++)
		{
			if (cb.objects.Get(i))
			{
				n++;
			}
		}
		return n;
	}

	static int Proxies(DZOMS_ClientBatch cb)
	{
		int n = 0;
		for (int i = 0; i < cb.proxies.Count(); i++)
		{
			if (cb.proxies.Get(i))
			{
				n++;
			}
		}
		return n;
	}

	// advance time and run one frame
	static void Frame(DZOMS_ClientWorld w, vector cam, int advanceMs)
	{
		s_Time += advanceMs;
		w.Update(cam, s_Time, true);
	}

	static void Run()
	{
		DZOMS_T.Suite("clientspawn");
		s_Time = 1000;
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 -800";

		array<ref DZOMS_ObjectEntry> list = new array<ref DZOMS_ObjectEntry>();
		for (int row = 0; row < 5; row++)
		{
			for (int col = 0; col < 50; col++)
			{
				list.Insert(new DZOMS_ObjectEntry("Apple", base + Vector(col * 0.1, 0, row * 0.1), "0 0 0", 1, 0));
			}
		}
		list.Insert(new DZOMS_ObjectEntry("Land_House_1W10", base + "10 0 10", "0 0 0", 1, DZOMS_Batch.FLAG_NETWORKED));
		for (int m = 0; m < 3; m++)
		{
			list.Insert(new DZOMS_ObjectEntry("DZOMS_No_Such_Class", base + Vector(2 + m, 0, 5), "0 0 0", 1, 0));
		}
		for (int far = 0; far < 10; far++)
		{
			list.Insert(new DZOMS_ObjectEntry("Apple", base + Vector(3000 + far * 0.1, 0, 0), "0 0 0", 1, 0));
		}
		array<ref DZOMS_Batch> sb = new array<ref DZOMS_Batch>();
		array<float> bo = new array<float>();
		DZOMS_BatchBuilder.Build(list, 400, sb, bo);
		DZOMS_T.EqInt(sb.Count(), 2, "2 batches");
		DZOMS_T.EqInt(sb.Get(0).Count(), 254, "batch 0 has 254 entries");
		DZOMS_Manifest manifest = DZOMS_Test_ClientDownload.MakeManifest(sb, bo, true);

		DZOMS_ClientWorld w = new DZOMS_ClientWorld("");
		w.m_Offline = true;
		w.Update(base, s_Time, true);
		w.HandleManifest(manifest, s_Time);
		for (int i = 0; i < 2; i++)
		{
			DZOMS_Batch c = DZOMS_Test_ClientDownload.Clone(sb.Get(i));
			c.index = i;
			w.HandleBatch(c, s_Time);
		}
		DZOMS_T.Check(w.IsComplete(), "all batches received");
		DZOMS_ClientBatch b0 = w.GetClientBatch(0);
		DZOMS_ClientBatch b1 = w.GetClientBatch(1);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.UNLOADED, "batch 0 UNLOADED before the first frame");

		Frame(w, base, 16);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADING, "first frame: batch 0 LOADING");
		DZOMS_T.EqInt(b1.state, DZOMS_ClientBatch.UNLOADED, "batch 1 stays UNLOADED");
		DZOMS_T.EqInt(Live(b0), 100, "first frame: 100 spawned");
		Frame(w, base, 16);
		DZOMS_T.EqInt(Live(b0), 200, "second frame: 200");
		Frame(w, base, 16);
		DZOMS_T.EqInt(Live(b0), 250, "third frame: 250");
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADED, "batch 0 LOADED");
		DZOMS_T.Check(b0.objects.Get(250) == null, "networked house is not spawned locally");
		DZOMS_T.Check(w.IsFailedType("DZOMS_No_Such_Class"), "missing class remembered");
		DZOMS_T.EqInt(Live(b1), 0, "batch 1 empty");
		Object firstApple = b0.objects.Get(0);

		// far away: proxy of the networked house, no network id
		vector far1200 = base + "1200 0 0";
		Frame(w, far1200, 1100);
		Frame(w, far1200, 16);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADED, "1200 m: still LOADED");
		DZOMS_T.EqInt(Proxies(b0), 1, "1200 m: proxy of the house");
		Object proxy = b0.proxies.Get(250);
		DZOMS_T.Check(proxy != null, "proxy slot is the house entry");
		if (proxy)
		{
			int lowP, highP;
			proxy.GetNetworkID(lowP, highP);
			DZOMS_T.Check(lowP == 0 && highP == 0, "proxy has no network id");
		}

		// the networked instance arrives: proxy goes
		Object net = g_Game.CreateObjectEx("Land_House_1W10", base + "10 0 10", ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD);
		DZOMS_T.Check(net != null, "networked house created");
		DZOMS_T.Check(DZOMS_ClientWorld.NetworkedInstancePresent("Land_House_1W10", base + "10 0 10", proxy), "networked instance detected");
		DZOMS_T.Check(!DZOMS_ClientWorld.NetworkedInstancePresent("Land_House_1W10", base + "10 0 10", net), "excluded object is not counted");
		Frame(w, far1200, 1100);
		Frame(w, far1200, 16);
		DZOMS_T.EqInt(Proxies(b0), 0, "proxy removed when the networked instance exists");
		g_Game.ObjectDelete(net);
		Frame(w, far1200, 1100);
		Frame(w, far1200, 16);
		DZOMS_T.EqInt(Proxies(b0), 1, "proxy comes back when the networked instance is gone");
		Frame(w, base, 1100);
		Frame(w, base, 16);
		DZOMS_T.EqInt(Proxies(b0), 0, "proxy removed when close");

		// hysteresis and unloading
		Frame(w, base + "1600 0 0", 300);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADED, "1600 m: kept (hysteresis)");
		Frame(w, base + "1800 0 0", 300);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.UNLOADING, "1800 m: UNLOADING");
		DZOMS_T.EqInt(Live(b0), 50, "200 deleted in one frame");
		Frame(w, base, 300);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADING, "back: LOADING again");
		Frame(w, base, 16);
		Frame(w, base, 16);
		Frame(w, base, 16);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADED, "LOADED again");
		DZOMS_T.EqInt(Live(b0), 250, "250 live, no duplicates");
		DZOMS_T.Check(b0.objects.Get(0) == firstApple, "survivor is the same object");

		Frame(w, base + "1800 0 0", 300);
		Frame(w, base + "1800 0 0", 16);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.UNLOADED, "UNLOADED after two frames");
		DZOMS_T.EqInt(Live(b0), 0, "nothing live");
		Frame(w, base, 300);
		Frame(w, base, 16);
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.LOADING, "loading again after UNLOADED");
		DZOMS_T.Check(Live(b0) > 0, "objects spawned again");
		w.Destroy();
		DZOMS_T.EqInt(Live(b0), 0, "Destroy removes everything");
		DZOMS_T.EqInt(b0.state, DZOMS_ClientBatch.UNLOADED, "Destroy resets the state");
	}
}
