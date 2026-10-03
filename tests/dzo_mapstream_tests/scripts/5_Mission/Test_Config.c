class DZOMS_Test_Config
{
	static void WriteText(string path, string text)
	{
		FileHandle fh = OpenFile(path, FileMode.WRITE);
		if (fh != 0)
		{
			FPrint(fh, text);
			CloseFile(fh);
		}
	}

	static string ReadText(string path)
	{
		string all = "";
		string line;
		FileHandle fh = OpenFile(path, FileMode.READ);
		if (fh == 0)
		{
			return all;
		}
		while (FGets(fh, line) >= 0)
		{
			all += line + "\n";
		}
		CloseFile(fh);
		return all;
	}

	static string CaseDir(string name)
	{
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(DZOMS_T.DIR + "/config");
		string dir = DZOMS_T.DIR + "/config/" + name;
		DeleteFile(dir + "/config.json");
		MakeDirectory(dir);
		return dir;
	}

	static void Run()
	{
		DZOMS_T.Suite("config");

		// nested directories are created level by level
		string deep = DZOMS_T.DIR + "/nest_a/nest_b";
		DZOMS_Config.MakeDirs(deep);
		DZOMS_T.Check(FileExist(deep) || FileExist(deep + "/."), "MakeDirs creates nested directories");

		// missing file: default written and loaded in the same start
		string dir = CaseDir("missing");
		DZOMS_Config c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(FileExist(dir + "/config.json"), "default config written");
		DZOMS_T.Check(FileExist(dir + "/DZE") || FileExist(dir + "/DZE/."), "DZE dir created");
		DZOMS_T.Check(!c.m_LoadFailed, "default not failed");
		DZOMS_T.EqInt(c.maps.Count(), 0, "default has no maps");
		DZOMS_T.EqInt(c.networked.Count(), 8, "8 default rules");
		if (c.networked.Count() == 8)
		{
			DZOMS_T.Eq(c.networked.Get(0), "@doors", "first rule");
			DZOMS_T.Eq(c.networked.Get(3), "@fixedItems", "4th rule");
			DZOMS_T.Eq(c.networked.Get(7), "*Stair*", "last rule");
		}
		DZOMS_T.EqFloat(c.viewDistance, 1500, "effective viewDistance");
		DZOMS_T.EqFloat(c.proxyDistance, 1000, "effective proxyDistance");
		DZOMS_T.EqInt(c.batchSize, 400, "effective batchSize");
		string written = ReadText(dir + "/config.json");
		DZOMS_T.Check(written.Contains("viewDistance") && !written.Contains("1500"), "file keeps viewDistance 0: " + written);

		// broken JSON: error, file untouched, defaults set, no maps
		dir = CaseDir("broken");
		WriteText(dir + "/config.json", "{ \"maps\": [ { \"file\": \"a.dze\" } ");
		string before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.m_LoadFailed, "broken json sets m_LoadFailed");
		DZOMS_T.EqInt(c.maps.Count(), 0, "broken json loads no maps");
		DZOMS_T.EqInt(c.batchSize, 400, "broken json: defaults");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "broken file untouched");

		// partial file
		dir = CaseDir("partial");
		WriteText(dir + "/config.json", "{ \"maps\": [ { \"file\": \"a.dze\" }, { \"file\": \"b.dze\", \"disabled\": true, \"networked\": [\"*X*\"] } ], \"batchSize\": 5, \"viewDistance\": -3, \"proxyDistance\": 99999 }");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(!c.m_LoadFailed, "partial not failed");
		DZOMS_T.EqInt(c.maps.Count(), 2, "partial: 2 maps");
		if (c.maps.Count() == 2)
		{
			DZOMS_T.Eq(c.maps.Get(0).file, "a.dze", "map 0 file");
			DZOMS_T.Check(!c.maps.Get(0).disabled, "map 0 enabled");
			DZOMS_T.Check(c.maps.Get(0).networked != null, "map 0 rules not NULL");
			DZOMS_T.Check(c.maps.Get(1).disabled, "map 1 disabled");
			DZOMS_T.EqInt(c.maps.Get(1).networked.Count(), 1, "map 1 rules");
		}
		DZOMS_T.Check(c.networked != null, "global rules not NULL");
		DZOMS_T.EqInt(c.networked.Count(), 0, "missing networked means no rules");
		DZOMS_T.EqInt(c.batchSize, 50, "batchSize clamped up");
		DZOMS_T.EqFloat(c.viewDistance, 100, "viewDistance clamped up");
		DZOMS_T.EqFloat(c.proxyDistance, 20000, "proxyDistance clamped down");

		// NULL map entry, batchSize too large
		dir = CaseDir("null");
		WriteText(dir + "/config.json", "{ \"maps\": [ null, { \"file\": \"a.dze\" } ], \"batchSize\": 5000 }");
		c = DZOMS_Config.Load(dir);
		// the loader turns a JSON null into an entry with an empty file (the server world warns about it)
		DZOMS_T.EqInt(c.maps.Count(), 2, "null map entry loads as an entry");
		if (c.maps.Count() == 2)
		{
			DZOMS_T.Check(c.maps.Get(0) != null && c.maps.Get(0).file == "", "null entry has an empty file");
			DZOMS_T.Check(c.maps.Get(0).networked != null, "null entry has rule array");
		}
		DZOMS_T.EqInt(c.batchSize, 1000, "batchSize clamped down");
	}
}
