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
	protected ref array<ref DZOMS_Batch> m_Batches = new array<ref DZOMS_Batch>();
	protected ref array<bool> m_Promoted = new array<bool>();	// per object: a networked copy exists (DESIGN 13.4)
	protected int m_PromotedCount;
	protected ref array<int> m_ObjectBatch = new array<int>();	// per object: its batch
	protected ref array<int> m_ObjectIndex = new array<int>();	// per object: its position inside that batch
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

	// tests: record broadcasts instead of sending them
	bool m_Offline;
	ref array<int> m_BroadcastTypes = new array<int>();
	ref array<ref DZOMS_IdList> m_BroadcastLists = new array<ref DZOMS_IdList>();

	// network id of the networked copy of a promoted object; false for a local object
	bool GetNetworkId(int i, out int low, out int high)
	{
		low = 0;
		high = 0;
		if (!IsPromoted(i) || !m_Spawned.Get(i))
		{
			return false;
		}
		m_Spawned.Get(i).GetNetworkID(low, high);
		return low != 0 || high != 0;
	}

	bool IsPromoted(int i)
	{
		if (i < 0 || i >= m_Promoted.Count())
		{
			return false;
		}
		return m_Promoted.Get(i);
	}

	// out-of-range indices are ignored
	void MarkPromoted(int i, bool value)
	{
		if (i < 0 || i >= m_Promoted.Count() || m_Promoted.Get(i) == value)
		{
			return;
		}
		m_Promoted.Set(i, value);
		if (value)
		{
			m_PromotedCount++;
		}
		else
		{
			m_PromotedCount--;
		}
	}

	int GetPromotedCount()
	{
		return m_PromotedCount;
	}

	// one message per 500 ids; target NULL = every client
	protected void Broadcast(int rpcType, DZOMS_IdList ids, PlayerIdentity target)
	{
		int total = ids.Count();
		int from = 0;
		while (from < total)
		{
			DZOMS_IdList chunk = new DZOMS_IdList();
			int to = Math.Min(from + DZOMS_IdList.MAX_IDS, total);
			for (int i = from; i < to; i++)
			{
				if (ids.HasNetIds())
				{
					chunk.AddNet(ids.batch.Get(i), ids.index.Get(i), ids.netLow.Get(i), ids.netHigh.Get(i));
				}
				else
				{
					chunk.Add(ids.batch.Get(i), ids.index.Get(i));
				}
			}
			if (m_Offline)
			{
				m_BroadcastTypes.Insert(rpcType);
				m_BroadcastLists.Insert(chunk);
			}
			else
			{
				ScriptRPC rpc = new ScriptRPC();
				rpc.Write(chunk);
				rpc.Send(null, rpcType, true, target);
			}
			from = to;
		}
	}

	void BroadcastNetworked(DZOMS_IdList ids)
	{
		Broadcast(DZOMS_Rpc.OBJECTS_NETWORKED, ids, null);
	}

	void BroadcastLocal(DZOMS_IdList ids)
	{
		Broadcast(DZOMS_Rpc.OBJECTS_LOCAL, ids, null);
	}

	// the objects that are networked now, for a client that just asked for the manifest
	void SendFullSet(PlayerIdentity target)
	{
		DZOMS_IdList all = new DZOMS_IdList();
		for (int i = 0; i < m_Promoted.Count(); i++)
		{
			int low, high;
			if (GetNetworkId(i, low, high))
			{
				all.AddNet(GetObjectBatch(i), GetObjectIndexInBatch(i), low, high);
			}
		}
		if (all.Count() > 0)
		{
			Broadcast(DZOMS_Rpc.OBJECTS_NETWORKED, all, target);
		}
	}

	protected ref DZOMS_Proximity m_Proximity;

	// after Load(): proximity networking for this world (DESIGN 13)
	void StartProximity()
	{
		m_Proximity = null;
		if (m_Config.disableProximity)
		{
			DZOMS_Log.Info("proximity networking is switched off in config.json");
			return;
		}
		m_Proximity = new DZOMS_Proximity();
		m_Proximity.Init(this);
	}

	DZOMS_Proximity GetProximity()
	{
		return m_Proximity;
	}

	// local object i -> networked copy (DESIGN 13.4). The networked copy exists before the local
	// one is deleted. False (nothing changed) when it cannot be spawned.
	bool Promote(int i)
	{
		Object old;
		if (!PromoteKeepOld(i, old))
		{
			return false;
		}
		g_Game.ObjectDelete(old);
		return true;
	}

	// as Promote, but the caller deletes the old local copy later: the path graph needs time to take the new
	// object in, and zombies walk through the gap when the old obstacle is gone before that
	bool PromoteKeepOld(int i, out Object oldLocal)
	{
		oldLocal = null;
		if (i < 0 || i >= m_Objects.Count() || m_Promoted.Get(i))
		{
			return false;
		}
		Object old = m_Spawned.Get(i);
		if (!old)
		{
			return false;
		}
		Object fresh = SpawnOnServerEx(m_Objects.Get(i), true, true);
		if (!fresh)
		{
			return false;
		}
		// a promoted item is decoration: simulated, it would settle on the ground instead of staying
		// where the map builder put it (found in game: items 0.4 m off after the swap)
		EntityAI promoted = EntityAI.Cast(fresh);
		if (promoted)
		{
			promoted.DisableSimulation(true);
		}
		oldLocal = old;
		m_Spawned.Set(i, fresh);
		MarkPromoted(i, true);
		return true;
	}

	// networked copy -> local object. The local copy is spawned first; the caller deletes oldNetworked
	// a moment later (the clients need their local copy back first). False (nothing changed) when the
	// local copy cannot be spawned.
	bool Demote(int i, out Object oldNetworked)
	{
		oldNetworked = null;
		if (i < 0 || i >= m_Objects.Count() || !m_Promoted.Get(i))
		{
			return false;
		}
		Object old = m_Spawned.Get(i);
		Object fresh = SpawnOnServerEx(m_Objects.Get(i), false, true);
		if (!fresh)
		{
			return false;
		}
		m_Spawned.Set(i, fresh);
		MarkPromoted(i, false);
		oldNetworked = old;
		return true;
	}

	int GetObjectCount()
	{
		return m_Objects.Count();
	}

	// batch of object i, -1 for an invalid index
	int GetObjectBatch(int i)
	{
		if (i < 0 || i >= m_ObjectBatch.Count())
		{
			return -1;
		}
		return m_ObjectBatch.Get(i);
	}

	int GetObjectIndexInBatch(int i)
	{
		if (i < 0 || i >= m_ObjectIndex.Count())
		{
			return -1;
		}
		return m_ObjectIndex.Get(i);
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
		return SpawnOnServerEx(e, networked, true);
	}

	static Object SpawnOnServerEx(DZOMS_ObjectEntry e, bool networked, bool pathgraph)
	{
		if (DZOMS_TypeName.IsModel(e.type))
		{
			// as the map file gives it, the engine puts the model's base on the position and takes radians;
			// an entry with FLAG_ORIGIN holds the object's origin (degrees) and is put there exactly. The
			// scale is passed to the creation: SetOrientation would reset it.
			bool origin = (e.flags & DZOMS_Batch.FLAG_ORIGIN) == DZOMS_Batch.FLAG_ORIGIN;
			vector rawOri = e.ori;
			if (origin)
			{
				rawOri = e.ori * Math.DEG2RAD;
			}
			Object model = g_Game.CreateStaticObjectUsingP3D(e.type, e.pos, rawOri, e.scale, !networked);
			if (model && origin)
			{
				DZOMS_Placement.Apply(model, e.pos, e.ori, e.scale);
			}
			return model;
		}

		int flags = ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR;
		if (pathgraph)
		{
			flags = flags | ECE_UPDATEPATHGRAPH;
		}
		if (!networked)
		{
			flags = flags | ECE_LOCAL;
		}
		Object obj = g_Game.CreateObjectEx(e.type, e.pos, flags);
		if (!obj)
		{
			return null;
		}
		obj.SetPosition(e.pos);
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

		map<string, ref DZOMS_MapEntry> overrides = new map<string, ref DZOMS_MapEntry>();
		CollectOverrides(overrides);

		map<string, bool> used = new map<string, bool>();
		array<string> files;
		DZOMS_MapFiles.List(m_MapsDir, files);
		if (files.Count() == 0)
		{
			DZOMS_Log.Info("no .dze files in " + m_MapsDir);
		}
		for (int i = 0; i < files.Count(); i++)
		{
			string file = files.Get(i);
			string key = file;
			key.ToLower();
			DZOMS_MapEntry entry = overrides.Get(key);
			if (entry && entry.disabled)
			{
				DZOMS_Log.Info("map " + file + " is disabled");
			}
			else
			{
				LoadMap(file, entry, globalRules);
			}
			used.Set(key, true);
		}
		// an entry that matched no file in the DZE folder
		for (int u = 0; u < overrides.Count(); u++)
		{
			DZOMS_MapEntry unused = overrides.GetElement(u);
			string unusedKey = overrides.GetKey(u);
			if (used.Contains(unusedKey))
			{
				continue;
			}
			if (unused.disabled)
			{
				DZOMS_Log.Info("map " + unused.file + " is disabled");
			}
			else
			{
				Reject(true, "config: maps entry '" + unused.file + "' has no file in " + m_MapsDir);
			}
		}

		SuppressHidden();

		// ECE_UPDATEPATHGRAPH only marks the objects; vanilla's object spawner and the Editor's loader
		// process the marks once after spawning, otherwise zombies ignore the objects
		int pathStart = TickCount(0);
		g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
		DZOMS_Log.Info("path graph updated for the map objects in " + (TickCount(pathStart) / 1000).ToString() + " ms");

		DZOMS_BatchBuilder.Build(m_Objects, m_Config.batchSize, m_Batches, m_Manifest.bounds, m_ObjectBatch, m_ObjectIndex);
		m_Promoted.Clear();
		m_PromotedCount = 0;
		for (int pi = 0; pi < m_Objects.Count(); pi++)
		{
			m_Promoted.Insert(false);
		}
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

	// maps[] entries by lower case file name; invalid and duplicate entries are rejected
	protected void CollectOverrides(map<string, ref DZOMS_MapEntry> overrides)
	{
		for (int i = 0; i < m_Config.maps.Count(); i++)
		{
			DZOMS_MapEntry entry = m_Config.maps.Get(i);
			if (!entry)
			{
				continue;
			}
			string file = entry.file;
			if (file == "")
			{
				Reject(false, "config: a maps entry has no file, ignored");
				continue;
			}
			if (file.Contains("..") || file.Contains("/") || file.Contains("\\") || file.Contains(":"))
			{
				Reject(true, "config: maps file '" + file + "' must be a plain file name inside DZE/, ignored");
				continue;
			}
			string lower = file;
			lower.ToLower();
			if (overrides.Contains(lower))
			{
				Reject(false, "config: maps file '" + file + "' is listed twice, ignored");
				continue;
			}
			overrides.Set(lower, entry);
		}
	}

	protected void LoadMap(string file, DZOMS_MapEntry entry, array<ref DZOMS_Rule> globalRules)
	{
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
		if (entry)
		{
			DZOMS_NetworkRules.Parse(entry.networked, mapRules, ruleErrors);
		}
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
			// the "Editor Only" property of the object (stored in the file). The EDITOR_ONLY bit of Flags
			// is not used: files from the Editor exist whose objects all carry it (flags 158) and are meant to be seen
			if (src.EditorOnly)
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
				e.flags = e.flags | DZOMS_Batch.FLAG_NETWORKED;
			}
			if (!spawned)
			{
				report.CountFailed(src.Type);
			}
			else if (DZOMS_TypeName.IsModel(e.type) && (e.flags & DZOMS_Batch.FLAG_ORIGIN) == 0)
			{
				// the Editor shows the object where the engine put it: that origin and its yaw in degrees
				// are what the clients and later respawns use
				e.pos = spawned.GetPosition();
				e.ori = spawned.GetOrientation();
				e.flags = e.flags | DZOMS_Batch.FLAG_ORIGIN;
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
			SendFullSet(sender);
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
