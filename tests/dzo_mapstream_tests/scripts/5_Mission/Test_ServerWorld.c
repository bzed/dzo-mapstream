class DZOMS_Test_ServerWorld
{
	static DZOMS_MapEntry Entry(string file, bool disabled, array<string> rules)
	{
		DZOMS_MapEntry e = new DZOMS_MapEntry();
		e.file = file;
		e.disabled = disabled;
		e.networked = rules;
		return e;
	}

	static DZOMS_Config MakeConfig()
	{
		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		array<string> r1 = {"*Container*"};
		array<string> r2 = {"!Land_Container_1Bo"};
		array<string> none = {};
		c.maps.Insert(Entry("w1.dze", false, r1));
		c.maps.Insert(Entry("w2.dze", false, r2));
		c.maps.Insert(Entry("w3.dze", true, none));
		c.maps.Insert(Entry("../evil.dze", false, none));
		c.maps.Insert(Entry("W1.DZE", false, none));
		c.maps.Insert(Entry("nope.dze", false, none));
		c.maps.Insert(Entry("", false, none));
		c.networked = {"@doors", "@ladders", "@actions", "@fixedItems"};
		c.Normalize();
		return c;
	}

	static void NetworkIdOk(Object o, bool wantNetworked, string what)
	{
		if (!o)
		{
			return;
		}
		int low, high;
		o.GetNetworkID(low, high);
		bool has = low != 0 || high != 0;
		DZOMS_T.Check(has == wantNetworked, what + ": network id present == " + wantNetworked.ToString());
		if (has && wantNetworked)
		{
			DZOMS_T.Check(g_Game.GetObjectByNetworkId(low, high) == o, what + ": network id resolves back");
		}
	}

	static void Run()
	{
		DZOMS_T.Suite("serverworld");
		DZOMS_NetworkRules.ClearTokenCache();
		string mapsDir = DZOMS_T.DIR + "/DZE_world";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(mapsDir);
		vector base = DZOMS_TestUtil.TEST_ORIGIN + "0 0 400";
		string model = DZOMS_TypeName.ModelPath("Land_House_1W10");
		model.Replace("/", "\\");

		Object building = DZOMS_TestFixtures.FindWorldBuilding();
		DZOMS_T.Check(building != null, "found a world building to hide");

		EditorSaveData w1 = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(w1, "Land_House_1W10", base, "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Land_Misc_Well_Pump_Blue", base + "20 0 0", "45 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "TerritoryFlag", base + "40 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Apple", base + "60 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Apple", base + "60.004 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Land_Container_1Bo", base + "80 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Land_House_1W10", base + "100 0 0", "0 0 0", 0, 0);
		DZOMS_TestFixtures.AddObject(w1, model, base + "120 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "DZOMS_No_Such_Class", base + "140 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w1, "Apple", base + "160 0 0", "0 0 0", 1, EditorObjectFlags.EDITOR_ONLY);
		// flag bit without the property (flags 158 as in real files): shown
		DZOMS_TestFixtures.AddObject(w1, "Apple", base + "170 0 0", "0 0 0", 1, 158);
		DZOMS_TestFixtures.AddObject(w1, "", base + "180 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddHidden(w1, building);
		DZOMS_T.Check(DZOMS_TestFixtures.Save(w1, mapsDir + "/w1.dze"), "w1 saved");

		EditorSaveData w2 = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(w2, "Land_Container_1Bo", base + "200 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(w2, "Land_House_1W10", base, "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddHidden(w2, building);
		DZOMS_T.Check(DZOMS_TestFixtures.Save(w2, mapsDir + "/w2.dze"), "w2 saved");

		DZOMS_ServerWorld world = new DZOMS_ServerWorld(MakeConfig(), mapsDir, new DZOMS_ObjectProbeWorld());
		world.Load();

		array<ref DZOMS_ObjectEntry> objs = world.GetObjects();
		array<Object> spawned = world.GetSpawned();
		DZOMS_T.EqInt(objs.Count(), 10, "10 objects");
		DZOMS_T.EqInt(spawned.Count(), 10, "10 spawn slots");
		DZOMS_T.EqInt(world.GetRejectedFiles(), 4, "4 rejected map entries");

		// object -> batch mapping (proximity networking addresses objects by batch and index)
		DZOMS_T.EqInt(world.GetObjectCount(), objs.Count(), "GetObjectCount");
		int mapBad = 0;
		for (int mo = 0; mo < objs.Count(); mo++)
		{
			int mb = world.GetObjectBatch(mo);
			int mi = world.GetObjectIndexInBatch(mo);
			array<ref DZOMS_Batch> allBatches = world.GetBatches();
			if (mb < 0 || mb >= allBatches.Count() || mi < 0 || mi >= allBatches.Get(mb).Count())
			{
				mapBad++;
			}
			else if (allBatches.Get(mb).positions.Get(mi) != objs.Get(mo).pos || allBatches.Get(mb).GetType(mi) != objs.Get(mo).type)
			{
				mapBad++;
			}
		}
		DZOMS_T.EqInt(mapBad, 0, "every object resolves to its batch entry");
		DZOMS_T.EqInt(world.GetObjectBatch(-1), -1, "batch of index -1");
		DZOMS_T.EqInt(world.GetObjectBatch(objs.Count()), -1, "batch of index == count");
		DZOMS_T.EqInt(world.GetObjectIndexInBatch(objs.Count() + 5), -1, "index in batch out of range");
		if (objs.Count() == 10)
		{
			// order: house, pump, flag, apple, container, house(scale 0), model, missing class, flag-bit apple, w2 container
			array<bool> wantNet = {true, true, true, false, true, true, false, false, false, false};
			array<string> names = {"house", "pump", "flag", "apple", "w1 container", "scale-0 house", "p3d object", "missing class", "flag-bit apple", "w2 container"};
			for (int i = 0; i < 10; i++)
			{
				bool isNet = (objs.Get(i).flags & DZOMS_Batch.FLAG_NETWORKED) == DZOMS_Batch.FLAG_NETWORKED;
				DZOMS_T.Check(isNet == wantNet.Get(i), names.Get(i) + ": networked == " + wantNet.Get(i).ToString());
				NetworkIdOk(spawned.Get(i), wantNet.Get(i), names.Get(i));
				if (i != 7)
				{
					DZOMS_T.Check(spawned.Get(i) != null, names.Get(i) + ": spawned");
				}
			}
			DZOMS_T.Check(spawned.Get(7) == null, "missing class: spawn is NULL");
			DZOMS_T.EqFloat(objs.Get(5).scale, 1, "scale 0 becomes 1 in the data");
			if (spawned.Get(5))
			{
				DZOMS_T.EqFloat(spawned.Get(5).GetScale(), 1, "scale 0 becomes 1 in the world");
			}
			DZOMS_T.EqFloat(objs.Get(1).ori[0], 45, "pump orientation kept in the data");
			if (spawned.Get(1))
			{
				DZOMS_T.EqFloat(spawned.Get(1).GetOrientation()[0], 45, "pump orientation kept in the world");
			}
		}

		DZOMS_T.EqInt(world.GetSuppressed().Count(), 1, "hidden building collected once");
		if (building)
		{
			DZOMS_T.Check(GetDayZGame().GetSuppressedObjectManager().IsSuppressed(building), "building is suppressed");
		}

		DZOMS_Manifest m = world.GetManifest();
		DZOMS_T.Check(m.IsValid(), "manifest valid");
		DZOMS_T.EqFloat(m.viewDistance, 1500, "manifest viewDistance");
		DZOMS_T.EqFloat(m.proxyDistance, 1000, "manifest proxyDistance");
		array<ref DZOMS_Batch> batches = world.GetBatches();
		DZOMS_T.EqInt(m.Count(), batches.Count(), "one hash per batch");
		int total = 0;
		for (int b = 0; b < batches.Count(); b++)
		{
			total += batches.Get(b).Count();
			// locals first: nested string-returning calls as arguments gave wrong values in the VM
			string recomputed = DZOMS_BatchHasher.Hash(batches.Get(b));
			string announced = m.hashes.Get(b);
			DZOMS_T.Eq(announced, recomputed, "hash of batch " + b.ToString());
		}
		DZOMS_T.EqInt(total, 10, "all objects in batches");
		string digest = world.GetDigest();
		DZOMS_T.EqInt(digest.Length(), 64, "digest");

		world.Destroy();
		if (building)
		{
			DZOMS_T.Check(!GetDayZGame().GetSuppressedObjectManager().IsSuppressed(building), "unsuppressed after Destroy");
		}

		DZOMS_NetworkRules.ClearTokenCache();
		DZOMS_ServerWorld again = new DZOMS_ServerWorld(MakeConfig(), mapsDir, new DZOMS_ObjectProbeWorld());
		again.Load();
		DZOMS_T.Eq(again.GetDigest(), digest, "second world has the same digest");
		again.Destroy();
		DZOMS_NetworkRules.ClearTokenCache();
	}
}
