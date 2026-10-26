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
		DeleteFile(dir + "/" + DZOMS_CacheKey.FILE);
		MakeDirectory(dir);
		return dir;
	}

	static void WriteKeyFile(string path, array<int> words)
	{
		FileSerializer ser = new FileSerializer();
		if (ser.Open(path, FileMode.WRITE))
		{
			ser.Write(words);
			ser.Close();
		}
	}

	static bool SameKey(array<int> a, array<int> b)
	{
		if (!a || !b || a.Count() != b.Count())
		{
			return false;
		}
		for (int i = 0; i < a.Count(); i++)
		{
			if (a.Get(i) != b.Get(i))
			{
				return false;
			}
		}
		return true;
	}

	// the words of "00 01 02 .. 1f" read big endian (the RFC test key of Test_ChaCha20 reads the same bytes little endian)
	static array<int> BigEndianKey()
	{
		return {66051, 67438087, 134810123, 202182159, 269554195, 336926231, 404298267, 471670303};
	}

	static bool AllZero(array<int> w)
	{
		for (int i = 0; i < w.Count(); i++)
		{
			if (w.Get(i) != 0)
			{
				return false;
			}
		}
		return true;
	}

	static void CacheKeyTests()
	{
		DZOMS_T.Suite("cachekey");
		string hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
		array<int> key = new array<int>();
		DZOMS_T.Check(DZOMS_CacheKey.Parse(hex, key), "Parse of 00..1f");
		DZOMS_T.EqInt(key.Count(), 8, "parsed word count");
		DZOMS_T.Check(SameKey(key, BigEndianKey()), "parsed words are 00..1f big endian");
		if (key.Count() == 8)
		{
			DZOMS_T.EqInt(key.Get(0), 66051, "first word big endian");
		}
		hex.ToUpper();
		DZOMS_T.Check(DZOMS_CacheKey.Parse(hex, key), "upper case accepted");
		DZOMS_T.EqInt(key.Count(), 8, "upper case: 8 words");
		DZOMS_T.Check(DZOMS_CacheKey.Parse("ffffffff00000000ffffffff00000000ffffffff00000000ffffffff00000000", key), "high bit words parse");
		if (key.Count() == 8)
		{
			DZOMS_T.EqInt(key.Get(0), -1, "ffffffff is -1");
			DZOMS_T.EqInt(key.Get(1), 0, "00000000 is 0");
		}
		string good = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
		DZOMS_T.Check(!DZOMS_CacheKey.Parse(good.Substring(0, 63), key), "63 characters");
		DZOMS_T.EqInt(key.Count(), 0, "63 characters: key empty");
		DZOMS_T.Check(!DZOMS_CacheKey.Parse(good + "0", key), "65 characters");
		DZOMS_T.EqInt(key.Count(), 0, "65 characters: key empty");
		DZOMS_T.Check(!DZOMS_CacheKey.Parse("g" + good.Substring(1, 63), key), "a g");
		DZOMS_T.EqInt(key.Count(), 0, "a g: key empty");
		DZOMS_T.Check(!DZOMS_CacheKey.Parse(" " + good.Substring(1, 63), key), "a space");
		DZOMS_T.EqInt(key.Count(), 0, "a space: key empty");
		DZOMS_T.Check(!DZOMS_CacheKey.Parse("", key), "empty string");

		array<int> r1 = DZOMS_CacheKey.RandomWords(8);
		array<int> r2 = DZOMS_CacheKey.RandomWords(8);
		DZOMS_T.EqInt(r1.Count(), 8, "RandomWords(8) count");
		DZOMS_T.EqInt(r2.Count(), 8, "second RandomWords(8) count");
		DZOMS_T.Check(!AllZero(r1) && !AllZero(r2), "random words not all zero");
		DZOMS_T.Check(!SameKey(r1, r2), "two random keys differ");
		DZOMS_T.EqInt(DZOMS_CacheKey.RandomWords(3).Count(), 3, "RandomWords(3) count");
		DZOMS_T.EqInt(DZOMS_CacheKey.RandomWords(16).Count(), 16, "RandomWords(16) count");

		// fresh dir: key generated and written, the same on the next load
		string dir = CaseDir("key_fresh");
		DZOMS_Config c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(FileExist(dir + "/" + DZOMS_CacheKey.FILE), "cachekey.bin written");
		DZOMS_T.Check(c.m_CacheKey != null && c.m_CacheKey.Count() == 8, "generated key has 8 words");
		DZOMS_Config c2 = DZOMS_Config.Load(dir);
		DZOMS_T.Check(SameKey(c.m_CacheKey, c2.m_CacheKey), "second load gives the same key");

		// broken key files are replaced
		dir = CaseDir("key_short");
		array<int> three = {1, 2, 3};
		WriteKeyFile(dir + "/" + DZOMS_CacheKey.FILE, three);
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.m_CacheKey != null && c.m_CacheKey.Count() == 8, "3-word file: 8 words");
		c2 = DZOMS_Config.Load(dir);
		DZOMS_T.Check(SameKey(c.m_CacheKey, c2.m_CacheKey), "3-word file: replaced, later load returns the new key");

		dir = CaseDir("key_zero");
		array<int> zeros = {0, 0, 0, 0, 0, 0, 0, 0};
		WriteKeyFile(dir + "/" + DZOMS_CacheKey.FILE, zeros);
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.m_CacheKey != null && c.m_CacheKey.Count() == 8 && !AllZero(c.m_CacheKey), "all-zero file: new key");
		c2 = DZOMS_Config.Load(dir);
		DZOMS_T.Check(SameKey(c.m_CacheKey, c2.m_CacheKey), "all-zero file: replaced, later load returns the new key");

		dir = CaseDir("key_garbage");
		WriteText(dir + "/" + DZOMS_CacheKey.FILE, "this is not a key file");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(c.m_CacheKey != null && c.m_CacheKey.Count() == 8 && !AllZero(c.m_CacheKey), "text file: new key");
		c2 = DZOMS_Config.Load(dir);
		DZOMS_T.Check(SameKey(c.m_CacheKey, c2.m_CacheKey), "text file: replaced, later load returns the new key");

		// a set key is used and no file is made
		dir = CaseDir("key_set");
		WriteText(dir + "/config.json", "{ \"cacheKey\": \"" + good + "\" }");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(SameKey(c.m_CacheKey, BigEndianKey()), "configured key is used");
		DZOMS_T.Check(!FileExist(dir + "/" + DZOMS_CacheKey.FILE), "configured key: no file");

		// an invalid key is reported and a generated one used
		dir = CaseDir("key_bad");
		WriteText(dir + "/config.json", "{ \"cacheKey\": \"xyz\" }");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.Check(!c.m_LoadFailed, "invalid cacheKey: config not failed");
		DZOMS_T.Check(c.m_CacheKey != null && c.m_CacheKey.Count() == 8 && !AllZero(c.m_CacheKey), "invalid cacheKey: generated key");
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

		// proximity networking options (DESIGN 13.7): zero values are the defaults, ranges are clamped
		dir = CaseDir("proximity_options");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.EqFloat(c.networkRadius, 150, "default networkRadius");
		DZOMS_T.EqFloat(c.vehicleRadius, 40, "default vehicleRadius");
		DZOMS_T.EqFloat(c.maxFootSpeed, 6, "default maxFootSpeed");
		DZOMS_T.EqFloat(c.maxVehicleSpeed, 8, "default maxVehicleSpeed");
		DZOMS_T.EqFloat(c.maxHeight, 20, "default maxHeight");
		DZOMS_T.EqInt(c.maxNetworked, 6000, "default maxNetworked");
		DZOMS_T.Check(c.proximityExclude != null && c.proximityExclude.Count() == 0, "default proximityExclude is empty");
		WriteText(dir + "/config.json", "{ \"networkRadius\": 3, \"vehicleRadius\": 9999, \"maxFootSpeed\": -1, \"maxHeight\": 5, \"maxNetworked\": 999999, \"proximityExclude\": [\"*Bush*\", \"@doors\", \"Land_X\"] }");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.EqFloat(c.networkRadius, 5, "networkRadius clamped up");
		DZOMS_T.EqFloat(c.vehicleRadius, 500, "vehicleRadius clamped down");
		DZOMS_T.EqFloat(c.maxFootSpeed, 6, "a negative speed means the default");
		DZOMS_T.EqFloat(c.maxHeight, 5, "maxHeight as given");
		DZOMS_T.EqInt(c.maxNetworked, 20000, "maxNetworked clamped down");
		DZOMS_T.EqInt(c.proximityExclude.Count(), 2, "proximityExclude drops @ tokens");
		if (c.proximityExclude.Count() == 2)
		{
			DZOMS_T.Eq(c.proximityExclude.Get(0), "*Bush*", "proximityExclude keeps patterns");
			DZOMS_T.Eq(c.proximityExclude.Get(1), "Land_X", "proximityExclude keeps names");
		}

		// a version 1 file without the new options is loaded as it is and not rewritten
		dir = CaseDir("proximity_old_file");
		WriteText(dir + "/config.json", "{ \"maps\": [], \"batchSize\": 100 }");
		before = ReadText(dir + "/config.json");
		c = DZOMS_Config.Load(dir);
		DZOMS_T.EqFloat(c.networkRadius, 150, "old file: default radius");
		DZOMS_T.Eq(ReadText(dir + "/config.json"), before, "old file: not rewritten");
		DZOMS_T.Check(!FileExist(dir + "/config.json.v1.bak"), "old file: no backup");

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

		CacheKeyTests();
	}
}
