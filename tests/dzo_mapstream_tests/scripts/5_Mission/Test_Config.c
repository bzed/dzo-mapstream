// Test migration: version 2 appends "test-rule" to the global list
class DZOMS_TestMigration: DZOMS_ConfigMigration
{
	override int CurrentVersion()
	{
		return 2;
	}

	override bool Step(DZOMS_Config c, int toVersion, out string what)
	{
		if (toVersion != 2)
		{
			return false;
		}
		if (!c.networked)
		{
			c.networked = new array<string>();
		}
		c.networked.Insert("test-rule");
		what = "added test-rule";
		return true;
	}
}

class DZOMS_TestMigrationNoBackup: DZOMS_TestMigration
{
	override bool Backup(string path, string backupPath)
	{
		return false;
	}
}

// wants version 3 but only knows the step to 2
class DZOMS_TestMigrationGap: DZOMS_TestMigration
{
	override int CurrentVersion()
	{
		return 3;
	}
}

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
		DeleteFile(dir + "/config.json.v1.bak");
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

		// proximity networking switch
		dir = CaseDir("proximity_off");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(!c.disableProximity, "proximity networking is on by default");
		WriteText(dir + "/config.json", "{ \"disableProximity\": true }");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.disableProximity, "proximity networking can be switched off");

		// config version (DESIGN 13.5)
		DZOMS_T.EqInt(DZOMS_Config.EffectiveVersion(0), 1, "EffectiveVersion 0 is 1");
		DZOMS_T.EqInt(DZOMS_Config.EffectiveVersion(1), 1, "EffectiveVersion 1 is 1");
		DZOMS_T.EqInt(DZOMS_Config.EffectiveVersion(3), 3, "EffectiveVersion 3 is 3");

		dir = CaseDir("version_default");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.EqInt(c.configVersion, DZOMS_Config.CURRENT_VERSION, "default file has the current version");
		DZOMS_T.Check(ReadText(dir + "/config.json").Contains("\"configVersion\": 1"), "default file text carries configVersion 1");
		DZOMS_T.Check(!c.m_NewerVersion, "default file is not newer");

		dir = CaseDir("version_missing");
		WriteText(dir + "/config.json", "{ \"maps\": [] }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.EqInt(DZOMS_Config.EffectiveVersion(c.configVersion), 1, "file without configVersion is version 1");
		DZOMS_T.Check(!c.m_NewerVersion, "file without configVersion is not newer");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "file without configVersion unchanged");
		DZOMS_T.Check(!FileExist(dir + "/config.json.v1.bak"), "no backup for a version 1 file");

		dir = CaseDir("version_newer");
		WriteText(dir + "/config.json", "{ \"configVersion\": 7, \"batchSize\": 123 }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.m_NewerVersion, "newer file sets m_NewerVersion");
		DZOMS_T.EqInt(c.batchSize, 123, "newer file keeps its other fields");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "newer file unchanged");

		// migration (DESIGN 13.5), with a test migration to version 2
		DZOMS_TestMigration mig = new DZOMS_TestMigration();
		dir = CaseDir("mig_basic");
		WriteText(dir + "/config.json", "{ \"networked\": [\"!Land_X\"], \"viewDistance\": 0 }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir, mig);
		DZOMS_T.EqInt(c.configVersion, 2, "migrated to version 2");
		DZOMS_T.EqInt(c.m_MigratedFrom, 1, "m_MigratedFrom is 1");
		DZOMS_T.Check(c.m_MigrationWritten, "migrated file written");
		DZOMS_T.EqInt(c.networked.Count(), 2, "migrated list has 2 rules");
		if (c.networked.Count() == 2)
		{
			DZOMS_T.Eq(c.networked.Get(0), "!Land_X", "admin rule kept first");
			DZOMS_T.Eq(c.networked.Get(1), "test-rule", "step rule added");
		}
		DZOMS_T.EqFloat(c.viewDistance, 1500, "viewDistance normalized in memory");
		written = ReadText(dir + "/config.json");
		DZOMS_T.Check(written.Contains("\"configVersion\": 2"), "file has configVersion 2: " + written);
		DZOMS_T.Check(written.Contains("test-rule"), "file has the step rule");
		DZOMS_T.Check(!written.Contains("1500"), "file keeps viewDistance 0 (written before Normalize)");
		DZOMS_T.Eq(ReadText(dir + "/config.json.v1.bak"), before, "backup equals the original");

		// second start: nothing to do
		string after = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir, mig);
		DZOMS_T.EqInt(c.m_MigratedFrom, 0, "second start does not migrate");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), after, "second start leaves the file");

		// file without networked
		dir = CaseDir("mig_nolist");
		WriteText(dir + "/config.json", "{ \"batchSize\": 100 }");
		c = DZOMS_Config.Load(dir, mig);
		DZOMS_T.EqInt(c.networked.Count(), 1, "NULL list: only the step rule");
		if (c.networked.Count() == 1)
		{
			DZOMS_T.Eq(c.networked.Get(0), "test-rule", "NULL list: step rule");
		}
		DZOMS_T.EqInt(c.batchSize, 100, "NULL list: other fields kept");

		// backup fails: migrated in memory, file untouched
		dir = CaseDir("mig_nobackup");
		WriteText(dir + "/config.json", "{ \"networked\": [\"A\"] }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir, new DZOMS_TestMigrationNoBackup());
		DZOMS_T.EqInt(c.configVersion, 2, "backup failure: migrated in memory");
		DZOMS_T.EqInt(c.networked.Count(), 2, "backup failure: step applied in memory");
		DZOMS_T.Check(!c.m_MigrationWritten, "backup failure: not written");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "backup failure: file text unchanged");

		// an existing backup is replaced
		dir = CaseDir("mig_oldbak");
		WriteText(dir + "/config.json.v1.bak", "stale");
		WriteText(dir + "/config.json", "{ \"networked\": [\"B\"] }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir, mig);
		DZOMS_T.Eq(ReadText(dir + "/config.json.v1.bak"), before, "existing backup replaced");

		// missing step: error, version reached, file not written
		dir = CaseDir("mig_gap");
		WriteText(dir + "/config.json", "{ \"networked\": [\"C\"] }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir, new DZOMS_TestMigrationGap());
		DZOMS_T.EqInt(c.configVersion, 2, "gap: stays at the version reached");
		DZOMS_T.Check(!c.m_MigrationWritten, "gap: not written");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "gap: file text unchanged");
	}
}
