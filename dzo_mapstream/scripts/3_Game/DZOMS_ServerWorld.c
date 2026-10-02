// Per map file counters and the networked report (DESIGN.md 5.5).
class DZOMS_MapReport
{
	string file;
	int networked;
	int localCount;
	int invalid;
	int editorOnly;
	int duplicates;
	int hiddenFound;
	int hiddenMissing;

	protected ref array<string> m_Keys = new array<string>();
	protected ref map<string, int> m_Counts = new map<string, int>();
	protected ref map<string, string> m_Rules = new map<string, string>();
	protected ref array<string> m_FailedOrder = new array<string>();
	protected ref map<string, int> m_Failed = new map<string, int>();

	void DZOMS_MapReport(string f)
	{
		file = f;
	}

	void CountObject(bool isNetworked, string type, string rule)
	{
		string kind = "L";
		if (isNetworked)
		{
			networked++;
			kind = "N";
		}
		else
		{
			localCount++;
		}
		string key = kind + "|" + type;
		int n;
		if (!m_Counts.Find(key, n))
		{
			m_Keys.Insert(key);
			m_Rules.Set(key, rule);
		}
		m_Counts.Set(key, n + 1);
	}

	void CountFailed(string type)
	{
		int n;
		if (!m_Failed.Find(type, n))
		{
			m_FailedOrder.Insert(type);
		}
		m_Failed.Set(type, n + 1);
	}

	void Print(bool debugLog)
	{
		string summary = file + ": " + networked.ToString() + " networked, " + localCount.ToString() + " local, ";
		summary += invalid.ToString() + " invalid, " + editorOnly.ToString() + " editor-only, " + duplicates.ToString() + " duplicates, ";
		summary += "hidden " + hiddenFound.ToString() + " found / " + hiddenMissing.ToString() + " not found";
		DZOMS_Log.Info(summary);

		for (int f = 0; f < m_FailedOrder.Count(); f++)
		{
			string failedType = m_FailedOrder.Get(f);
			DZOMS_Log.Warn(file + ": could not spawn " + failedType + " x" + m_Failed.Get(failedType).ToString());
		}

		if (!debugLog)
		{
			return;
		}
		for (int i = 0; i < m_Keys.Count(); i++)
		{
			string key = m_Keys.Get(i);
			string type = key.Substring(2, key.Length() - 2);
			string line = file + " ";
			string rule = m_Rules.Get(key);
			if (key.Get(0) == "N")
			{
				line += "networked: " + type + " x" + m_Counts.Get(key).ToString() + " (" + rule + ")";
				DZOMS_Log.Info(line);
			}
			else if (rule != "")
			{
				line += "local by rule: " + type + " x" + m_Counts.Get(key).ToString() + " (" + rule + ")";
				DZOMS_Log.Info(line);
			}
		}
	}
}

// Loads the maps, spawns every object on the server (networked or local), suppresses hidden
// world objects, builds the batches and the manifest (DESIGN.md 6).
class DZOMS_ServerWorld
{
	static const int ANSWER_NONE = 0;
	static const int ANSWER_MANIFEST = 1;
	static const int ANSWER_BATCH = 2;

	protected ref DZOMS_Config m_Config;
	protected string m_MapsDir;
	protected ref DZOMS_ObjectProbe m_Probe;

	protected ref array<ref DZOMS_ObjectEntry> m_Objects = new array<ref DZOMS_ObjectEntry>();
	protected ref array<Object> m_Spawned = new array<Object>();
	protected ref array<Object> m_Suppressed = new array<Object>();
	protected ref map<Object, bool> m_SuppressedSet = new map<Object, bool>();
	protected ref map<string, bool> m_SeenObjects = new map<string, bool>();
	protected ref map<string, bool> m_SeenFiles = new map<string, bool>();
	protected ref array<ref DZOMS_Batch> m_Batches = new array<ref DZOMS_Batch>();
	protected ref DZOMS_Manifest m_Manifest = new DZOMS_Manifest();
	protected string m_Digest;
	protected int m_RejectedFiles;
	protected ref DZOMS_RateLimiter m_RateLimiter = new DZOMS_RateLimiter();

	void DZOMS_ServerWorld(DZOMS_Config config, string mapsDir, DZOMS_ObjectProbe probe)
	{
		m_Config = config;
		m_MapsDir = mapsDir;
		m_Probe = probe;
		if (!m_Probe)
		{
			m_Probe = new DZOMS_ObjectProbe();
		}
	}

	DZOMS_Config GetConfig()
	{
		return m_Config;
	}

	array<ref DZOMS_ObjectEntry> GetObjects()
	{
		return m_Objects;
	}

	array<Object> GetSpawned()
	{
		return m_Spawned;
	}

	array<Object> GetSuppressed()
	{
		return m_Suppressed;
	}

	array<ref DZOMS_Batch> GetBatches()
	{
		return m_Batches;
	}

	DZOMS_Manifest GetManifest()
	{
		return m_Manifest;
	}

	string GetDigest()
	{
		return m_Digest;
	}

	// map entries that were rejected (empty, unsafe, duplicate, missing or unreadable file)
	int GetRejectedFiles()
	{
		return m_RejectedFiles;
	}

	static Object SpawnOnServer(DZOMS_ObjectEntry e, bool networked)
	{
		if (DZOMS_TypeName.IsModel(e.type))
		{
			return g_Game.CreateStaticObjectUsingP3D(e.type, e.pos, e.ori, e.scale, !networked);
		}

		int flags = ECE_SETUP | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR;
		if (!networked)
		{
			flags = flags | ECE_LOCAL;
		}
		Object obj = g_Game.CreateObjectEx(e.type, e.pos, flags);
		if (!obj)
		{
			return null;
		}
		obj.SetOrientation(e.ori);
		if (e.scale != 1)
		{
			obj.SetScale(e.scale);
		}
		obj.Update();
		return obj;
	}

	void Load()
	{
		array<ref DZOMS_Rule> globalRules = new array<ref DZOMS_Rule>();
		array<string> ruleErrors = new array<string>();
		DZOMS_NetworkRules.Parse(m_Config.networked, globalRules, ruleErrors);
		LogRuleErrors("networked", ruleErrors);

		for (int i = 0; i < m_Config.maps.Count(); i++)
		{
			DZOMS_MapEntry entry = m_Config.maps.Get(i);
			if (entry)
			{
				LoadMap(entry, globalRules);
			}
		}

		SuppressHidden();

		DZOMS_BatchBuilder.Build(m_Objects, m_Config.batchSize, m_Batches, m_Manifest.bounds);
		m_Manifest.viewDistance = m_Config.viewDistance;
		m_Manifest.proxyDistance = m_Config.proxyDistance;
		m_Manifest.disableCache = m_Config.disableCache;
		for (int b = 0; b < m_Batches.Count(); b++)
		{
			m_Manifest.hashes.Insert(DZOMS_BatchHasher.Hash(m_Batches.Get(b)));
		}
		m_Digest = DZOMS_BatchHasher.Digest(m_Manifest.hashes);

		int networkedCount = 0;
		for (int o = 0; o < m_Objects.Count(); o++)
		{
			if ((m_Objects.Get(o).flags & DZOMS_Batch.FLAG_NETWORKED) == DZOMS_Batch.FLAG_NETWORKED)
			{
				networkedCount++;
			}
		}
		int localCount = m_Objects.Count() - networkedCount;
		DZOMS_Log.Info(m_Objects.Count().ToString() + " objects (" + networkedCount.ToString() + " networked, " + localCount.ToString() + " local) in " + m_Batches.Count().ToString() + " batches, digest " + m_Digest);

		if (m_Config.debugLog)
		{
			for (int d = 0; d < m_Batches.Count(); d++)
			{
				string line = "batch " + d.ToString() + ": " + m_Batches.Get(d).Count().ToString() + " objects, center " + m_Manifest.GetCenter(d).ToString();
				line += ", radius " + m_Manifest.GetRadius(d).ToString() + ", hash " + m_Manifest.hashes.Get(d);
				DZOMS_Log.Info(line);
			}
		}
	}

	protected void LogRuleErrors(string where, array<string> errors)
	{
		for (int i = 0; i < errors.Count(); i++)
		{
			DZOMS_Log.Err("config error in " + where + ": " + errors.Get(i));
		}
	}

	protected void Reject(bool asError, string msg)
	{
		m_RejectedFiles++;
		if (asError)
		{
			DZOMS_Log.Err(msg);
		}
		else
		{
			DZOMS_Log.Warn(msg);
		}
	}

	protected void LoadMap(DZOMS_MapEntry entry, array<ref DZOMS_Rule> globalRules)
	{
		string file = entry.file;
		if (file == "")
		{
			Reject(false, "config: a map entry has no file, ignored");
			return;
		}
		if (entry.disabled)
		{
			DZOMS_Log.Info("map " + file + " is disabled");
			return;
		}
		if (file.Contains("..") || file.Contains("/") || file.Contains("\\") || file.Contains(":"))
		{
			Reject(true, "config: map file '" + file + "' must be a plain file name inside maps/, ignored");
			return;
		}
		string lower = file;
		lower.ToLower();
		if (m_SeenFiles.Contains(lower))
		{
			Reject(false, "config: map file '" + file + "' is listed twice, ignored");
			return;
		}
		m_SeenFiles.Set(lower, true);

		string path = m_MapsDir + "/" + file;
		if (!FileExist(path))
		{
			Reject(true, "map file " + path + " does not exist");
			return;
		}
		EditorSaveData data = EditorSaveData.LoadFromFile(path);
		if (!data)
		{
			Reject(true, "cannot load map file " + path);
			return;
		}

		array<ref DZOMS_Rule> mapRules = new array<ref DZOMS_Rule>();
		array<string> ruleErrors = new array<string>();
		DZOMS_NetworkRules.Parse(entry.networked, mapRules, ruleErrors);
		LogRuleErrors(file, ruleErrors);
		DZOMS_NetworkRules rules = DZOMS_NetworkRules.Combine(globalRules, mapRules);

		DZOMS_MapReport report = new DZOMS_MapReport(file);
		LoadObjects(data, rules, report);
		CollectHidden(data, report);
		report.Print(m_Config.debugLog);
	}

	protected void LoadObjects(EditorSaveData data, DZOMS_NetworkRules rules, DZOMS_MapReport report)
	{
		for (int i = 0; i < data.EditorObjects.Count(); i++)
		{
			EditorObjectData src = data.EditorObjects.Get(i);
			if (!src || src.Type == "")
			{
				report.invalid++;
				continue;
			}
			// EditorOnly is [NonSerialized] and always false after loading: only Flags is reliable
			if ((src.Flags & EditorObjectFlags.EDITOR_ONLY) == EditorObjectFlags.EDITOR_ONLY)
			{
				report.editorOnly++;
				continue;
			}

			vector pos = src.Position;
			int cx = Math.Round(pos[0] * 100);
			int cy = Math.Round(pos[1] * 100);
			int cz = Math.Round(pos[2] * 100);
			string key = DZOMS_TypeName.Of(src.Type) + "|" + cx.ToString() + "|" + cy.ToString() + "|" + cz.ToString();
			if (m_SeenObjects.Contains(key))
			{
				report.duplicates++;
				continue;
			}
			m_SeenObjects.Set(key, true);

			float scale = src.Scale;
			if (scale <= 0)
			{
				scale = 1;
			}
			DZOMS_ObjectEntry e = new DZOMS_ObjectEntry(src.Type, pos, src.Orientation, scale, 0);
			string rule;
			Object spawned;
			bool isNetworked = SpawnEntry(e, rules, spawned, rule);
			if (isNetworked)
			{
				e.flags = DZOMS_Batch.FLAG_NETWORKED;
			}
			if (!spawned)
			{
				report.CountFailed(src.Type);
			}
			report.CountObject(isNetworked, src.Type, rule);
			m_Objects.Insert(e);
			m_Spawned.Insert(spawned);
		}
	}

	// returns true if the final decision is "networked"; spawned is NULL when the spawn failed
	protected bool SpawnEntry(DZOMS_ObjectEntry e, DZOMS_NetworkRules rules, out Object spawned, out string rule)
	{
		int decision = rules.Decide(e.type, rule);
		spawned = null;
		if (decision == DZOMS_NetworkRules.NEEDS_OBJECT)
		{
			spawned = SpawnOnServer(e, false);
			decision = rules.DecideWithObject(e.type, spawned, m_Probe, rule);
			if (decision == DZOMS_NetworkRules.NETWORKED)
			{
				if (spawned)
				{
					g_Game.ObjectDelete(spawned);
				}
				spawned = SpawnOnServer(e, true);
			}
			return decision == DZOMS_NetworkRules.NETWORKED;
		}

		bool isNetworked = decision == DZOMS_NetworkRules.NETWORKED;
		spawned = SpawnOnServer(e, isNetworked);
		return isNetworked;
	}

	protected void CollectHidden(EditorSaveData data, DZOMS_MapReport report)
	{
		for (int i = 0; i < data.EditorHiddenObjects.Count(); i++)
		{
			EditorDeletedObjectData hidden = data.EditorHiddenObjects.Get(i);
			if (!hidden)
			{
				continue;
			}
			Object obj = hidden.FindObject();
			if (!obj)
			{
				report.hiddenMissing++;
				continue;
			}
			if (m_SuppressedSet.Contains(obj))
			{
				continue;
			}
			m_SuppressedSet.Set(obj, true);
			m_Suppressed.Insert(obj);
			report.hiddenFound++;
		}
	}

	protected void SuppressHidden()
	{
		if (m_Suppressed.Count() == 0)
		{
			return;
		}
		DayZGame game = GetDayZGame();
		SuppressedObjectManager manager = null;
		if (game)
		{
			manager = game.GetSuppressedObjectManager();
		}
		if (!manager)
		{
			DZOMS_Log.Err("Dabs Framework suppressed object manager is not available, hidden objects stay visible");
			return;
		}
		manager.SuppressMany(m_Suppressed);
	}

	DZOMS_RateLimiter GetRateLimiter()
	{
		return m_RateLimiter;
	}

	// validation and limits only, no network: tests call this directly
	int HandleRequest(string senderId, int rpcType, int value, int nowMs)
	{
		if (rpcType == DZOMS_Rpc.REQUEST_MANIFEST)
		{
			if (!m_RateLimiter.AllowManifest(senderId, nowMs))
			{
				return ANSWER_NONE;
			}
			if (value != DZOMS_Rpc.PROTOCOL_VERSION)
			{
				DZOMS_Log.Warn("client " + senderId + " speaks protocol " + value.ToString() + ", server speaks " + DZOMS_Rpc.PROTOCOL_VERSION.ToString() + ": same mod version needed on both sides");
				return ANSWER_NONE;
			}
			return ANSWER_MANIFEST;
		}
		if (rpcType == DZOMS_Rpc.REQUEST_BATCH)
		{
			if (value < 0 || value >= m_Batches.Count())
			{
				return ANSWER_NONE;
			}
			if (!m_RateLimiter.AllowBatch(senderId, nowMs))
			{
				return ANSWER_NONE;
			}
			return ANSWER_BATCH;
		}
		return ANSWER_NONE;
	}

	void OnRpc(PlayerIdentity sender, int rpcType, ParamsReadContext ctx)
	{
		if (!sender)
		{
			return;
		}
		int value;
		if (!ctx.Read(value))
		{
			return;
		}
		int answer = HandleRequest(sender.GetId(), rpcType, value, g_Game.GetTime());
		if (answer == ANSWER_MANIFEST)
		{
			ScriptRPC manifestRpc = new ScriptRPC();
			manifestRpc.Write(m_Manifest);
			manifestRpc.Send(null, DZOMS_Rpc.MANIFEST, true, sender);
		}
		else if (answer == ANSWER_BATCH)
		{
			ScriptRPC batchRpc = new ScriptRPC();
			batchRpc.Write(m_Batches.Get(value));
			batchRpc.Send(null, DZOMS_Rpc.BATCH, true, sender);
		}
	}

	void OnPlayerDisconnected(string uid)
	{
		m_RateLimiter.Remove(uid);
	}

	void SweepRateLimits(int nowMs)
	{
		m_RateLimiter.Sweep(nowMs);
	}

	// deletes everything this world spawned and unsuppresses the hidden objects (tests)
	void Destroy()
	{
		for (int i = 0; i < m_Spawned.Count(); i++)
		{
			if (m_Spawned.Get(i))
			{
				g_Game.ObjectDelete(m_Spawned.Get(i));
			}
		}
		m_Spawned.Clear();
		m_Objects.Clear();

		if (m_Suppressed.Count() > 0)
		{
			DayZGame game = GetDayZGame();
			if (game && game.GetSuppressedObjectManager())
			{
				game.GetSuppressedObjectManager().UnsupressMany(m_Suppressed);
			}
		}
		m_Suppressed.Clear();
		m_SuppressedSet.Clear();
	}
}
