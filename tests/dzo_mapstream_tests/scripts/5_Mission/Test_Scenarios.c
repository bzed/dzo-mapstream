// Scenario specific preparation and checks of the production server world (see tools/run-server-tests.sh).
class DZOMS_Test_Scenarios
{
	static void Prepare(string scenario)
	{
		if (scenario == "navprobe")
		{
			DZOMS_NavProbe.WriteWall();
			return;
		}
		if (scenario != "fixtures")
		{
			return;
		}
		DZOMS_Config.MakeDirs(DZOMS_Config.MAPS_DIR);
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 800";

		EditorSaveData a = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(a, "Land_House_1W10", at, "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(a, "Land_Misc_Well_Pump_Blue", at + "30 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(a, "TerritoryFlag", at + "50 0 0", "0 0 0", 1, 0);
		DZOMS_TestFixtures.AddObject(a, "Land_Container_1Bo", at + "70 0 0", "0 0 0", 1, 0);
		for (int i = 0; i < 60; i++)
		{
			int col = i % 10;
			int row = i / 10;
			DZOMS_TestFixtures.AddObject(a, "Apple", at + Vector(col * 10, 0, 100 + row * 10), "0 0 0", 1, 0);
		}
		// "reversed": the same contents under swapped names, so the alphabetical load order flips
		bool reversed = FileExist(DZOMS_T.DIR + "/reversed");
		string nameA = "fixture_a.dze";
		string nameB = "fixture_b.dze";
		if (reversed)
		{
			nameA = "fixture_b.dze";
			nameB = "fixture_a.dze";
		}
		DZOMS_TestFixtures.Save(a, DZOMS_Config.MAPS_DIR + "/" + nameA);

		EditorSaveData b = new EditorSaveData();
		DZOMS_TestFixtures.AddObject(b, "Land_Container_1Bo", at + "90 0 0", "0 0 0", 1, 0);
		for (int j = 0; j < 20; j++)
		{
			DZOMS_TestFixtures.AddObject(b, "Apple", at + Vector(400 + j, 0, 0), "0 0 0", 1, 0);
		}
		DZOMS_TestFixtures.Save(b, DZOMS_Config.MAPS_DIR + "/" + nameB);
	}

	static void Check(string scenario)
	{
		DZOMS_T.Suite("scenario-" + scenario);
		DZOMS_ServerWorld w = GetDayZGame().DZOMS_GetServerWorld();
		DZOMS_T.Check(w != null, "production server world exists");
		if (!w)
		{
			return;
		}
		DZOMS_Config c = w.GetConfig();
		if (scenario == "unit")
		{
			DZOMS_T.Check(!c.m_LoadFailed, "config loaded");
			DZOMS_T.EqInt(w.GetObjects().Count(), 0, "no objects");
			DZOMS_T.EqInt(w.GetManifest().Count(), 0, "empty manifest");
		}
		else if (scenario == "noconfig")
		{
			DZOMS_T.Check(FileExist(DZOMS_Config.PROFILE_DIR + "/config.json"), "default config written");
			DZOMS_T.Check(!c.m_LoadFailed, "default config loaded");
			DZOMS_T.EqInt(c.networked.Count(), 8, "8 default rules");
			DZOMS_T.EqFloat(c.viewDistance, 1500, "effective view distance");
		}
		else if (scenario == "broken")
		{
			DZOMS_T.Check(c.m_LoadFailed, "broken config flagged");
			DZOMS_T.EqInt(w.GetObjects().Count(), 0, "no objects");
		}
		else if (scenario == "realdze")
		{
			// tests/dze/*.dze: a file written by the real DayZ Editor, found in the DZE folder
			DZOMS_T.Check(!c.m_LoadFailed, "config loaded");
			DZOMS_T.EqInt(w.GetRejectedFiles(), 0, "no rejected file");
			DZOMS_T.Check(w.GetObjects().Count() > 0, "objects loaded from the real .dze: " + w.GetObjects().Count().ToString());
			DZOMS_T.Check(w.GetBatches().Count() > 0, "batches built");
			DZOMS_T.EqInt(w.GetManifest().Count(), w.GetBatches().Count(), "one hash per batch");
			int missing = 0;
			for (int r = 0; r < w.GetSpawned().Count(); r++)
			{
				if (!w.GetSpawned().Get(r))
				{
					missing++;
				}
			}
			DZOMS_T.EqInt(missing, 0, "every object spawned");
		}
		else if (scenario == "fixtures")
		{
			array<ref DZOMS_ObjectEntry> objs = w.GetObjects();
			array<Object> spawned = w.GetSpawned();
			DZOMS_T.EqInt(objs.Count(), 85, "85 objects");
			DZOMS_T.Check(w.GetBatches().Count() >= 3, "at least 3 batches");
			int networked = 0;
			for (int i = 0; i < objs.Count(); i++)
			{
				Object o = spawned.Get(i);
				DZOMS_T.Check(o != null, "object " + i.ToString() + " spawned");
				bool isNet = (objs.Get(i).flags & DZOMS_Batch.FLAG_NETWORKED) == DZOMS_Batch.FLAG_NETWORKED;
				if (isNet)
				{
					networked++;
				}
				if (o)
				{
					int low, high;
					o.GetNetworkID(low, high);
					bool hasId = low != 0 || high != 0;
					DZOMS_T.Check(hasId == isNet, "object " + i.ToString() + " network id matches its mode");
					if (isNet && hasId)
					{
						DZOMS_T.Check(g_Game.GetObjectByNetworkId(low, high) == o, "object " + i.ToString() + " resolves by network id");
					}
				}
			}
			DZOMS_T.EqInt(networked, 4, "4 networked objects");
		}
	}
}
