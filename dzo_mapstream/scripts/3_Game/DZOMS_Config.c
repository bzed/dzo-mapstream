// Admin configuration (DESIGN.md 5). Every field is designed so that its zero value
// is the default: JsonFileLoader zero-initializes fields that are missing from the file.
class DZOMS_MapEntry
{
	string file;
	bool disabled;
	ref array<string> networked;
}

class DZOMS_Config
{
	static const string PROFILE_DIR = "$profile:dzo-mapstream";
	static const float DEFAULT_VIEW_DISTANCE = 1500;
	static const float DEFAULT_PROXY_DISTANCE = 1000;
	static const int DEFAULT_BATCH_SIZE = 400;
	static const int MIN_BATCH_SIZE = 50;
	static const int MAX_BATCH_SIZE = 1000;
	static const float MIN_DISTANCE = 100;
	static const float MAX_DISTANCE = 20000;

	ref array<ref DZOMS_MapEntry> maps;
	ref array<string> networked;
	float viewDistance;
	float proxyDistance;
	int batchSize;
	bool disableCache;
	bool debugLog;

	[NonSerialized()]
	bool m_LoadFailed;

	static DZOMS_Config CreateDefault()
	{
		DZOMS_Config c = new DZOMS_Config();
		c.maps = new array<ref DZOMS_MapEntry>();
		c.networked = {"@doors", "@ladders", "@actions", "@fixedItems", "Land_Underground_Panel*", "*WaterSpring*", "*Switch*", "*Stair*"};
		return c;
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

	// never NULL. Missing file: write the default, then load it like any other file.
	// Invalid JSON: log, leave the file untouched, return an empty config with m_LoadFailed.
	static DZOMS_Config Load(string dir)
	{
		MakeDirectory(dir);
		MakeDirectory(dir + "/maps");
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
		c.Normalize();
		return c;
	}
}
