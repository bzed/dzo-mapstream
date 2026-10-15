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
	// proximity networking defaults and limits (DESIGN 13.7)
	static const float DEFAULT_NETWORK_RADIUS = 150;
	static const float DEFAULT_VEHICLE_RADIUS = 40;
	static const float MIN_ZONE_RADIUS = 5;
	static const float MAX_ZONE_RADIUS = 500;
	static const float DEFAULT_MAX_FOOT_SPEED = 6;
	static const float DEFAULT_MAX_VEHICLE_SPEED = 8;
	static const float DEFAULT_MAX_HEIGHT = 20;
	static const int DEFAULT_MAX_NETWORKED = 6000;
	static const int MAX_NETWORKED_LIMIT = 20000;
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
	// proximity networking (DESIGN 13): a networked copy of the map objects around a player who can use them
	// (climbing, sounds); on unless disabled (objects then stay local and cannot be climbed)
	bool disableProximity;
	float networkRadius;	// player on foot, m
	float vehicleRadius;	// slow vehicle, m
	float maxFootSpeed;	// m/s; faster on foot: no networked objects
	float maxVehicleSpeed;	// m/s
	float maxHeight;	// m above the ground; higher: none
	int maxNetworked;	// most objects networked at once
	ref array<string> proximityExclude;	// types that are never networked (name patterns, no @ tokens)

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

	protected static float NormalizeRadius(float v, float fallback, string name)
	{
		if (v == 0)
		{
			return fallback;
		}
		float clamped = Math.Clamp(v, MIN_ZONE_RADIUS, MAX_ZONE_RADIUS);
		if (clamped != v)
		{
			DZOMS_Log.Warn(name + " " + v.ToString() + " is outside " + MIN_ZONE_RADIUS.ToString() + ".." + MAX_ZONE_RADIUS.ToString() + ", using " + clamped.ToString());
		}
		return clamped;
	}

	protected void NormalizeProximity()
	{
		networkRadius = NormalizeRadius(networkRadius, DEFAULT_NETWORK_RADIUS, "networkRadius");
		vehicleRadius = NormalizeRadius(vehicleRadius, DEFAULT_VEHICLE_RADIUS, "vehicleRadius");
		if (maxFootSpeed <= 0)
		{
			maxFootSpeed = DEFAULT_MAX_FOOT_SPEED;
		}
		if (maxVehicleSpeed <= 0)
		{
			maxVehicleSpeed = DEFAULT_MAX_VEHICLE_SPEED;
		}
		if (maxHeight <= 0)
		{
			maxHeight = DEFAULT_MAX_HEIGHT;
		}
		if (maxNetworked == 0)
		{
			maxNetworked = DEFAULT_MAX_NETWORKED;
		}
		int limited = Math.Clamp(maxNetworked, 1, MAX_NETWORKED_LIMIT);
		if (limited != maxNetworked)
		{
			DZOMS_Log.Warn("maxNetworked " + maxNetworked.ToString() + " is outside 1.." + MAX_NETWORKED_LIMIT.ToString() + ", using " + limited.ToString());
			maxNetworked = limited;
		}
		if (!proximityExclude)
		{
			proximityExclude = new array<string>();
		}
		for (int i = proximityExclude.Count() - 1; i >= 0; i--)
		{
			string pattern = proximityExclude.Get(i);
			pattern.TrimInPlace();
			if (pattern == "" || pattern.Get(0) == "@")
			{
				DZOMS_Log.Err("config error in proximityExclude: '" + proximityExclude.Get(i) + "' is not a type name pattern, ignored");
				proximityExclude.RemoveOrdered(i);
			}
		}
	}

	void Normalize()
	{
		NormalizeProximity();
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
