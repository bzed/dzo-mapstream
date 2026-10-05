// Admin configuration (DESIGN.md 5). Every field is designed so that its zero value
// is the default: JsonFileLoader zero-initializes fields that are missing from the file.
// Optional per-file settings for a file in the DZE folder. Files without an entry are loaded
// with the global rules only.
class DZOMS_MapEntry
{
	string file;
	bool disabled;
	ref array<string> networked;
}

class DZOMS_Config
{
	static const string PROFILE_DIR = "$profile:dzo/mapstream";
	static const string MAPS_DIR = "$profile:dzo/mapstream/DZE";
	static const float DEFAULT_VIEW_DISTANCE = 1500;
	static const float DEFAULT_PROXY_DISTANCE = 1000;
	static const int DEFAULT_BATCH_SIZE = 400;
	static const int MIN_BATCH_SIZE = 50;
	static const int MAX_BATCH_SIZE = 1000;
	static const float MIN_DISTANCE = 100;
	static const float MAX_DISTANCE = 20000;
	// version of the file layout (DESIGN 5.6); a missing value (0) means version 1
	static const int CURRENT_VERSION = 1;

	int configVersion;

	ref array<ref DZOMS_MapEntry> maps;
	ref array<string> networked;
	float viewDistance;
	float proxyDistance;
	int batchSize;
	bool disableCache;
	bool debugLog;
	// proximity networking is on unless this is set: objects then stay local and cannot be climbed
	bool disableProximity;

	[NonSerialized()]
	bool m_LoadFailed;

	// the file has a newer version than this mod knows: used as it is, never written
	[NonSerialized()]
	bool m_NewerVersion;

	// version the file had before it was migrated (0 = not migrated) and whether the migrated
	// file was written
	[NonSerialized()]
	int m_MigratedFrom;
	[NonSerialized()]
	bool m_MigrationWritten;

	static int EffectiveVersion(int fileValue)
	{
		if (fileValue == 0)
		{
			return 1;
		}
		return fileValue;
	}

	static DZOMS_Config CreateDefault()
	{
		DZOMS_Config c = new DZOMS_Config();
		c.configVersion = CURRENT_VERSION;
		c.maps = new array<ref DZOMS_MapEntry>();
		c.networked = {"@doors", "@ladders", "@actions", "@fixedItems", "Land_Underground_Panel*", "*WaterSpring*", "*Switch*", "*Stair*"};
		return c;
	}

	// MakeDirectory creates one level only: create every level of "$profile:a/b/c"
	static void MakeDirs(string path)
	{
		array<string> parts = new array<string>();
		path.Split("/", parts);
		string current = "";
		for (int i = 0; i < parts.Count(); i++)
		{
			if (parts.Get(i) == "")
			{
				continue;
			}
			if (current != "")
			{
				current += "/";
			}
			current += parts.Get(i);
			MakeDirectory(current);
		}
	}

	protected static float NormalizeDistance(float v, float fallback, string name)
	{
		if (v == 0)
		{
			return fallback;
		}
		float clamped = Math.Clamp(v, MIN_DISTANCE, MAX_DISTANCE);
		if (clamped != v)
		{
			DZOMS_Log.Warn(name + " " + v.ToString() + " is outside " + MIN_DISTANCE.ToString() + ".." + MAX_DISTANCE.ToString() + ", using " + clamped.ToString());
		}
		return clamped;
	}

	void Normalize()
	{
		if (!maps)
		{
			maps = new array<ref DZOMS_MapEntry>();
		}
		if (!networked)
		{
			networked = new array<string>();
		}
		for (int i = maps.Count() - 1; i >= 0; i--)
		{
			DZOMS_MapEntry e = maps.Get(i);
			if (!e)
			{
				DZOMS_Log.Warn("config: maps[" + i.ToString() + "] is null, ignored");
				maps.RemoveOrdered(i);
			}
			else if (!e.networked)
			{
				e.networked = new array<string>();
			}
		}

		viewDistance = NormalizeDistance(viewDistance, DEFAULT_VIEW_DISTANCE, "viewDistance");
		proxyDistance = NormalizeDistance(proxyDistance, DEFAULT_PROXY_DISTANCE, "proxyDistance");

		if (batchSize == 0)
		{
			batchSize = DEFAULT_BATCH_SIZE;
		}
		int sized = Math.Clamp(batchSize, MIN_BATCH_SIZE, MAX_BATCH_SIZE);
		if (sized != batchSize)
		{
			DZOMS_Log.Warn("batchSize " + batchSize.ToString() + " is outside " + MIN_BATCH_SIZE.ToString() + ".." + MAX_BATCH_SIZE.ToString() + ", using " + sized.ToString());
			batchSize = sized;
		}
	}

	protected void Migrate(string path, DZOMS_ConfigMigration migration)
	{
		int from = EffectiveVersion(configVersion);
		int target = migration.CurrentVersion();
		if (from >= target)
		{
			return;
		}

		int v = from;
		string what;
		string steps = "";
		bool complete = true;
		while (v < target)
		{
			if (!migration.Step(this, v + 1, what))
			{
				DZOMS_Log.Err("config: no migration step to version " + (v + 1).ToString() + ", config.json is used at version " + v.ToString() + " and not written");
				complete = false;
				break;
			}
			v++;
			if (steps != "")
			{
				steps += ", ";
			}
			steps += what;
		}
		configVersion = v;
		if (v == from)
		{
			return;
		}
		m_MigratedFrom = from;
		if (!complete)
		{
			return;
		}

		string backupPath = path + ".v" + from.ToString() + ".bak";
		if (!migration.Backup(path, backupPath))
		{
			DZOMS_Log.Err("config: cannot back up " + path + " to " + backupPath + ", the migrated config is used for this start and the file is not written");
			return;
		}
		string error;
		if (!JsonFileLoader<DZOMS_Config>.SaveFile(path, this, error))
		{
			DZOMS_Log.Err("config: cannot write migrated " + path + ": " + error);
			return;
		}
		m_MigrationWritten = true;
		DZOMS_Log.Info("config: migrated config.json from version " + from.ToString() + " to " + v.ToString() + " (" + steps + "), backup " + backupPath);
	}

	// never NULL. Missing file: write the default, then load it like any other file.
	// Invalid JSON: log, leave the file untouched, return an empty config with m_LoadFailed.
	static DZOMS_Config Load(string dir)
	{
		return Load(dir, new DZOMS_ConfigMigration());
	}

	// Older version: migrate step by step, back up the file, write the migrated file (DESIGN 5.6).
	static DZOMS_Config Load(string dir, DZOMS_ConfigMigration migration)
	{
		MakeDirs(dir);
		MakeDirectory(dir + "/DZE");
		string path = dir + "/config.json";
		string error;

		if (!FileExist(path))
		{
			DZOMS_Config def = CreateDefault();
			if (JsonFileLoader<DZOMS_Config>.SaveFile(path, def, error))
			{
				DZOMS_Log.Info("config: wrote default " + path);
			}
			else
			{
				DZOMS_Log.Err("config: cannot write default " + path + ": " + error);
			}
		}

		DZOMS_Config c = new DZOMS_Config();
		if (!JsonFileLoader<DZOMS_Config>.LoadFile(path, c, error))
		{
			DZOMS_Log.Err("config error in " + path + ": " + error);
			c = new DZOMS_Config();
			c.m_LoadFailed = true;
		}
		else if (EffectiveVersion(c.configVersion) > CURRENT_VERSION)
		{
			DZOMS_Log.Err("config.json has version " + c.configVersion.ToString() + ", this mod knows up to " + CURRENT_VERSION.ToString() + ": update the mod; the file is used as it is and not changed");
			c.m_NewerVersion = true;
		}
		else
		{
			c.Migrate(path, migration);
		}
		c.Normalize();
		return c;
	}
}
