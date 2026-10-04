// One manifest entry on the client: the received data plus what has been spawned from it.
class DZOMS_ClientBatch
{
	static const int NOT_RECEIVED = 0;
	static const int UNLOADED = 1;
	static const int LOADING = 2;
	static const int LOADED = 3;
	static const int UNLOADING = 4;

	ref DZOMS_Batch data;
	int state;
	vector center;
	float radius;
	float distance;
	ref array<Object> objects;	// parallel to data, NULL for networked entries and failed spawns
	ref array<Object> proxies;	// distant proxies of networked entries
	ref array<int> networked;	// indices of the networked entries
	int cursor;
	int unloadCursor;

	void SetData(DZOMS_Batch b)
	{
		data = b;
		state = UNLOADED;
		objects = new array<Object>();
		proxies = new array<Object>();
		networked = new array<int>();
		for (int i = 0; i < b.Count(); i++)
		{
			objects.Insert(null);
			proxies.Insert(null);
			if (b.IsNetworked(i))
			{
				networked.Insert(i);
			}
		}
		cursor = 0;
		unloadCursor = 0;
	}
}

// Client side of the stream (DESIGN.md 8 and 9): downloads the manifest and the batches,
// verifies them against the manifest hashes, keeps the cache, and spawns batches by distance.
class DZOMS_ClientWorld
{
	static const int REQUEST_TIMEOUT_MS = 5000;
	static const int MANIFEST_INTERVAL_MS = 3000;
	static const int PAUSE_MS = 30000;
	static const int MAX_TIMEOUTS = 5;
	static const int MANIFEST_WARN_AFTER = 10;
	static const int DISTANCE_INTERVAL_MS = 250;
	static const float UNLOAD_HYSTERESIS = 200;
	static const int SPAWNS_PER_FRAME = 100;
	static const int DELETES_PER_FRAME = 200;
	static const int STATUS_INTERVAL_MS = 30000;
	static const int PROXY_INTERVAL_MS = 1000;
	static const int PROXIES_PER_FRAME = 50;
	static const float PROXY_HYSTERESIS = 50;
	// networked copies announced by the server (DESIGN 13.5)
	static const int SWAP_TIMEOUT_MS = 10000;
	static const int SWAPS_PER_FRAME = 20;
	static const int RESPAWNS_PER_FRAME = 50;
	static const int ID_FACTOR = 1024;
	static const int MAX_ANNOUNCED = 4000;

	int m_MaxAnnounced = MAX_ANNOUNCED;	// tests lower it
	protected ref map<int, bool> m_NetworkedIds = new map<int, bool>();	// key batch * ID_FACTOR + index
	protected ref array<int> m_PendingKeys = new array<int>();
	protected ref array<int> m_PendingSince = new array<int>();
	protected ref array<int> m_PendingLow = new array<int>();	// network id of the announced twin
	protected ref array<int> m_PendingHigh = new array<int>();
	protected ref array<int> m_RespawnKeys = new array<int>();
	protected int m_SwapTimeouts;
	protected int m_SwapsDone;
	protected int m_TwinMismatch;
	protected ref map<string, int> m_TwinLogged = new map<string, int>();
	protected int m_AnnounceDropped;

	// test seam: when set, requests are recorded here instead of being sent
	bool m_Offline;
	ref array<int> m_SentTypes = new array<int>();
	ref array<int> m_SentValues = new array<int>();

	protected string m_CachePath;
	protected bool m_CacheLoaded;
	protected ref map<string, ref DZOMS_Batch> m_Cache = new map<string, ref DZOMS_Batch>();

	protected ref DZOMS_Manifest m_Manifest;
	protected ref array<ref DZOMS_ClientBatch> m_Batches = new array<ref DZOMS_ClientBatch>();
	protected vector m_Camera;
	protected int m_Received;
	protected int m_Downloaded;
	protected int m_FromCache;
	protected bool m_Complete;

	protected int m_ManifestRequests;
	protected int m_LastManifestRequest;
	protected bool m_ManifestWarned;
	protected int m_InFlight = -1;
	protected int m_InFlightSince;
	protected int m_Timeouts;
	protected bool m_Paused;
	protected int m_PausedUntil;

	protected int m_LastStatus;
	protected bool m_HaveDistances;
	protected int m_LastDistances;
	protected ref map<string, bool> m_FailedTypes = new map<string, bool>();
	protected bool m_ProxyStarted;
	protected bool m_ProxyActive;
	protected int m_ProxyBatch;
	protected int m_ProxyPos;
	protected int m_LastProxyPass;

	void DZOMS_ClientWorld(string cachePath)
	{
		m_CachePath = cachePath;
		if (m_CachePath != "")
		{
			LoadCache();
		}
	}

	// "" if the host address is not known (yet)
	static string CachePathForHost()
	{
		string address;
		int port;
		if (!g_Game.GetHostAddress(address, port) || address == "")
		{
			return "";
		}
		address.Replace(":", "-");
		return DZOMS_Config.PROFILE_DIR + "/cache/" + address + "_" + port.ToString() + ".bin";
	}

	DZOMS_Manifest GetManifest()
	{
		return m_Manifest;
	}

	int GetBatchCount()
	{
		return m_Batches.Count();
	}

	DZOMS_ClientBatch GetClientBatch(int i)
	{
		return m_Batches.Get(i);
	}

	int GetReceivedCount()
	{
		return m_Received;
	}

	int GetDownloadedCount()
	{
		return m_Downloaded;
	}

	int GetFromCacheCount()
	{
		return m_FromCache;
	}

	bool IsComplete()
	{
		return m_Complete;
	}

	int GetInFlight()
	{
		return m_InFlight;
	}

	protected void LoadCache()
	{
		m_CacheLoaded = true;
		array<ref DZOMS_Batch> list = new array<ref DZOMS_Batch>();
		if (!DZOMS_Cache.Read(m_CachePath, list))
		{
			return;
		}
		for (int i = 0; i < list.Count(); i++)
		{
			string hash = DZOMS_BatchHasher.Hash(list.Get(i));
			m_Cache.Set(hash, list.Get(i));
		}
		DZOMS_Log.Info("cache: " + list.Count().ToString() + " batches read from " + m_CachePath);
	}

	protected void Send(int rpcType, int value)
	{
		if (m_Offline)
		{
			m_SentTypes.Insert(rpcType);
			m_SentValues.Insert(value);
			return;
		}
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(value);
		rpc.Send(null, rpcType, true, null);
	}

	void Update(vector camera, int nowMs, bool inGame)
	{
		if (!inGame)
		{
			return;
		}
		m_Camera = camera;

		if (!m_Manifest)
		{
			if (m_ManifestRequests == 0 || (nowMs - m_LastManifestRequest) >= MANIFEST_INTERVAL_MS)
			{
				m_ManifestRequests++;
				m_LastManifestRequest = nowMs;
				Send(DZOMS_Rpc.REQUEST_MANIFEST, DZOMS_Rpc.PROTOCOL_VERSION);
				if (m_ManifestRequests > MANIFEST_WARN_AFTER && !m_ManifestWarned)
				{
					m_ManifestWarned = true;
					DZOMS_Log.Warn("no manifest after " + MANIFEST_WARN_AFTER.ToString() + " requests: the server may not run this mod version");
				}
			}
			return;
		}

		DownloadStep(nowMs);
		if (m_Batches.Count() == 0)
		{
			return;
		}
		UpdateDistances(nowMs);
		SpawnStep();
		SwapStep(nowMs);
		RespawnStep();
		UnloadStep();
		ProxyStep(nowMs);
		LogStatus(nowMs);
	}

	// every 30 s: where the camera is and what is loaded, to tell "nothing in range" from "spawned but invisible"
	protected void LogStatus(int nowMs)
	{
		if (m_LastStatus != 0 && (nowMs - m_LastStatus) < STATUS_INTERVAL_MS)
		{
			return;
		}
		m_LastStatus = nowMs;
		int loaded = 0;
		int objects = 0;
		int received = 0;
		float nearest = -1;
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			DZOMS_ClientBatch cb = m_Batches.Get(i);
			if (cb.state == DZOMS_ClientBatch.NOT_RECEIVED)
			{
				continue;
			}
			received++;
			if (nearest < 0 || cb.distance < nearest)
			{
				nearest = cb.distance;
			}
			if (cb.state == DZOMS_ClientBatch.LOADED)
			{
				loaded++;
			}
			for (int j = 0; j < cb.objects.Count(); j++)
			{
				if (cb.objects.Get(j))
				{
					objects++;
				}
			}
		}
		DZOMS_Log.Info("status: camera " + m_Camera.ToString() + ", " + received.ToString() + "/" + m_Batches.Count().ToString() + " batches received, nearest " + Math.Round(nearest).ToString() + " m away (view distance " + Math.Round(m_Manifest.viewDistance).ToString() + " m), " + loaded.ToString() + " loaded, " + objects.ToString() + " local objects alive");
		DZOMS_Log.Info("swaps: " + m_SwapsDone.ToString() + " done, " + m_SwapTimeouts.ToString() + " timed out, " + m_PendingKeys.Count().ToString() + " pending, " + m_TwinMismatch.ToString() + " displaced twins, " + m_NetworkedIds.Count().ToString() + " announced");
	}

	bool IsFailedType(string type)
	{
		return m_FailedTypes.Contains(type);
	}

	protected void MarkFailed(string type)
	{
		if (!m_FailedTypes.Contains(type))
		{
			m_FailedTypes.Set(type, true);
			DZOMS_Log.Warn("cannot spawn " + type + " (content mod missing?), skipped from now on");
		}
	}

	// local object as the client spawns it (DESIGN.md 9.4); NULL if the type does not exist
	static Object SpawnLocal(string type, vector pos, vector ori, float scale)
	{
		if (DZOMS_TypeName.IsModel(type))
		{
			return g_Game.CreateStaticObjectUsingP3D(type, pos, ori, scale, true);
		}
		Object obj = g_Game.CreateObjectEx(type, pos, ECE_LOCAL | ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME);
		if (!obj)
		{
			return null;
		}
		obj.SetOrientation(ori);
		if (scale != 1)
		{
			obj.SetScale(scale);
		}
		obj.Update();
		return obj;
	}

	// a networked object of that type at that position (within 5 cm), other than exclude
	static bool NetworkedInstancePresent(string type, vector pos, Object exclude)
	{
		array<Object> found = new array<Object>();
		g_Game.GetObjectsAtPosition3D(pos, 0.5, found, null);
		string want = DZOMS_TypeName.Of(type);
		for (int i = 0; i < found.Count(); i++)
		{
			Object o = found.Get(i);
			if (!o || o == exclude)
			{
				continue;
			}
			int low, high;
			o.GetNetworkID(low, high);
			if (low == 0 && high == 0)
			{
				continue;
			}
			if (vector.Distance(o.GetPosition(), pos) > 0.05)
			{
				continue;
			}
			if (DZOMS_TypeName.Of(o.GetType()) == want || DZOMS_TypeName.Of(o.GetShapeName()) == want)
			{
				return true;
			}
		}
		return false;
	}

	protected void UpdateDistances(int nowMs)
	{
		if (m_HaveDistances && (nowMs - m_LastDistances) < DISTANCE_INTERVAL_MS)
		{
			return;
		}
		m_HaveDistances = true;
		m_LastDistances = nowMs;
		float view = m_Manifest.viewDistance;
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			DZOMS_ClientBatch cb = m_Batches.Get(i);
			if (cb.state == DZOMS_ClientBatch.NOT_RECEIVED)
			{
				continue;
			}
			cb.distance = Math.Max(0, vector.Distance(m_Camera, cb.center) - cb.radius);
			if ((cb.state == DZOMS_ClientBatch.UNLOADED || cb.state == DZOMS_ClientBatch.UNLOADING) && cb.distance <= view)
			{
				cb.state = DZOMS_ClientBatch.LOADING;
				cb.cursor = 0;
			}
			else if ((cb.state == DZOMS_ClientBatch.LOADED || cb.state == DZOMS_ClientBatch.LOADING) && cb.distance > view + UNLOAD_HYSTERESIS)
			{
				cb.state = DZOMS_ClientBatch.UNLOADING;
				cb.unloadCursor = cb.objects.Count() - 1;
			}
		}
	}

	// the batch in the given state with the smallest (or largest) distance, -1 if none
	protected int PickBatch(int state, bool nearest)
	{
		int best = -1;
		float bestDistance = 0;
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			DZOMS_ClientBatch cb = m_Batches.Get(i);
			if (cb.state != state)
			{
				continue;
			}
			bool better = best < 0 || (nearest && cb.distance < bestDistance) || (!nearest && cb.distance > bestDistance);
			if (better)
			{
				best = i;
				bestDistance = cb.distance;
			}
		}
		return best;
	}

	// at most SPAWNS_PER_FRAME spawn attempts per frame, nearest LOADING batch first
	protected void SpawnStep()
	{
		int budget = SPAWNS_PER_FRAME;
		while (budget > 0)
		{
			int pick = PickBatch(DZOMS_ClientBatch.LOADING, true);
			if (pick < 0)
			{
				return;
			}
			DZOMS_ClientBatch cb = m_Batches.Get(pick);
			int count = cb.data.Count();
			if (cb.cursor == 0)
			{
				DZOMS_Log.Info("batch " + pick.ToString() + ": spawning " + count.ToString() + " objects, " + Math.Round(cb.distance).ToString() + " m away");
			}
			while (cb.cursor < count && budget > 0)
			{
				int i = cb.cursor;
				cb.cursor++;
				if (cb.data.IsNetworked(i) || cb.objects.Get(i) || m_NetworkedIds.Contains(pick * ID_FACTOR + i))
				{
					continue;
				}
				string type = cb.data.GetType(i);
				if (m_FailedTypes.Contains(type))
				{
					continue;
				}
				budget--;
				Object o = SpawnLocal(type, cb.data.positions.Get(i), cb.data.orientations.Get(i), cb.data.scales.Get(i));
				if (o)
				{
					cb.objects.Set(i, o);
				}
				else
				{
					MarkFailed(type);
				}
			}
			if (cb.cursor >= count)
			{
				cb.state = DZOMS_ClientBatch.LOADED;
				int alive = 0;
				for (int k = 0; k < count; k++)
				{
					if (cb.objects.Get(k))
					{
						alive++;
					}
				}
				DZOMS_Log.Info("batch " + pick.ToString() + ": loaded, " + alive.ToString() + " of " + (count - cb.networked.Count()).ToString() + " local objects spawned (" + cb.networked.Count().ToString() + " networked entries are left to the server)");
			}
		}
	}

	// at most DELETES_PER_FRAME deleted objects and proxies per frame, farthest UNLOADING batch first
	protected void UnloadStep()
	{
		int budget = DELETES_PER_FRAME;
		while (budget > 0)
		{
			int pick = PickBatch(DZOMS_ClientBatch.UNLOADING, false);
			if (pick < 0)
			{
				return;
			}
			DZOMS_ClientBatch cb = m_Batches.Get(pick);
			while (cb.unloadCursor >= 0 && budget > 0)
			{
				int i = cb.unloadCursor;
				cb.unloadCursor--;
				Object o = cb.objects.Get(i);
				if (o)
				{
					g_Game.ObjectDelete(o);
					cb.objects.Set(i, null);
					budget--;
				}
				Object p = cb.proxies.Get(i);
				if (p)
				{
					g_Game.ObjectDelete(p);
					cb.proxies.Set(i, null);
					budget--;
				}
			}
			if (cb.unloadCursor < 0)
			{
				cb.state = DZOMS_ClientBatch.UNLOADED;
			}
		}
	}

	// a pass over the networked entries of LOADED batches every second, PROXIES_PER_FRAME entries per frame
	protected void ProxyStep(int nowMs)
	{
		if (!m_ProxyActive)
		{
			if (m_ProxyStarted && (nowMs - m_LastProxyPass) < PROXY_INTERVAL_MS)
			{
				return;
			}
			m_ProxyStarted = true;
			m_ProxyActive = true;
			m_ProxyBatch = 0;
			m_ProxyPos = 0;
			m_LastProxyPass = nowMs;
		}

		int budget = PROXIES_PER_FRAME;
		while (budget > 0 && m_ProxyBatch < m_Batches.Count())
		{
			DZOMS_ClientBatch cb = m_Batches.Get(m_ProxyBatch);
			if (cb.state != DZOMS_ClientBatch.LOADED || m_ProxyPos >= cb.networked.Count())
			{
				m_ProxyBatch++;
				m_ProxyPos = 0;
				continue;
			}
			ProxyEntry(cb, cb.networked.Get(m_ProxyPos));
			m_ProxyPos++;
			budget--;
		}
		if (m_ProxyBatch >= m_Batches.Count())
		{
			m_ProxyActive = false;
		}
	}

	protected void ProxyEntry(DZOMS_ClientBatch cb, int entry)
	{
		vector pos = cb.data.positions.Get(entry);
		string type = cb.data.GetType(entry);
		float d = vector.Distance(m_Camera, pos);
		float proxyDistance = m_Manifest.proxyDistance;
		Object proxy = cb.proxies.Get(entry);
		if (!proxy)
		{
			if (d > proxyDistance + PROXY_HYSTERESIS && !m_FailedTypes.Contains(type) && !NetworkedInstancePresent(type, pos, null))
			{
				Object created = SpawnLocal(type, pos, cb.data.orientations.Get(entry), cb.data.scales.Get(entry));
				if (created)
				{
					cb.proxies.Set(entry, created);
				}
				else
				{
					MarkFailed(type);
				}
			}
			return;
		}
		if (d < proxyDistance - PROXY_HYSTERESIS || NetworkedInstancePresent(type, pos, proxy))
		{
			g_Game.ObjectDelete(proxy);
			cb.proxies.Set(entry, null);
		}
	}

	// deletes every spawned object and proxy; received batches go back to UNLOADED
	void Destroy()
	{
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			DZOMS_ClientBatch cb = m_Batches.Get(i);
			if (cb.state == DZOMS_ClientBatch.NOT_RECEIVED)
			{
				continue;
			}
			for (int j = 0; j < cb.objects.Count(); j++)
			{
				if (cb.objects.Get(j))
				{
					g_Game.ObjectDelete(cb.objects.Get(j));
					cb.objects.Set(j, null);
				}
				if (cb.proxies.Get(j))
				{
					g_Game.ObjectDelete(cb.proxies.Get(j));
					cb.proxies.Set(j, null);
				}
			}
			cb.state = DZOMS_ClientBatch.UNLOADED;
			cb.cursor = 0;
			cb.unloadCursor = 0;
		}
	}

	protected void DownloadStep(int nowMs)
	{
		if (m_Complete)
		{
			return;
		}
		if (m_Paused)
		{
			if (nowMs < m_PausedUntil)
			{
				return;
			}
			m_Paused = false;
			m_Timeouts = 0;
			m_InFlight = -1;
		}
		if (m_InFlight >= 0)
		{
			if ((nowMs - m_InFlightSince) >= REQUEST_TIMEOUT_MS)
			{
				m_Timeouts++;
				if (m_Timeouts >= MAX_TIMEOUTS)
				{
					DZOMS_Log.Warn("batch " + m_InFlight.ToString() + " timed out " + m_Timeouts.ToString() + " times in a row, pausing for 30 s");
					m_Paused = true;
					m_PausedUntil = nowMs + PAUSE_MS;
					return;
				}
				RequestBatch(m_InFlight, nowMs);
			}
			return;
		}
		RequestNext(nowMs);
	}

	protected void RequestBatch(int index, int nowMs)
	{
		m_InFlight = index;
		m_InFlightSince = nowMs;
		Send(DZOMS_Rpc.REQUEST_BATCH, index);
	}

	// the not yet received batch closest to the camera
	protected void RequestNext(int nowMs)
	{
		int best = -1;
		float bestDistance = 0;
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			DZOMS_ClientBatch cb = m_Batches.Get(i);
			if (cb.state != DZOMS_ClientBatch.NOT_RECEIVED)
			{
				continue;
			}
			float d = Math.Max(0, vector.Distance(m_Camera, cb.center) - cb.radius);
			if (best < 0 || d < bestDistance)
			{
				best = i;
				bestDistance = d;
			}
		}
		if (best >= 0)
		{
			RequestBatch(best, nowMs);
		}
	}

	void OnRpc(int rpcType, ParamsReadContext ctx)
	{
		if (rpcType == DZOMS_Rpc.MANIFEST)
		{
			DZOMS_Manifest m = new DZOMS_Manifest();
			if (!ctx.Read(m) || !m)
			{
				return;
			}
			HandleManifest(m, g_Game.GetTime());
		}
		else if (rpcType == DZOMS_Rpc.BATCH)
		{
			DZOMS_Batch b = new DZOMS_Batch();
			if (!ctx.Read(b) || !b)
			{
				return;
			}
			HandleBatch(b, g_Game.GetTime());
		}
		else if (rpcType == DZOMS_Rpc.OBJECTS_NETWORKED || rpcType == DZOMS_Rpc.OBJECTS_LOCAL)
		{
			DZOMS_IdList ids = new DZOMS_IdList();
			if (!ctx.Read(ids) || !ids)
			{
				return;
			}
			if (rpcType == DZOMS_Rpc.OBJECTS_NETWORKED)
			{
				HandleNetworked(ids, g_Game.GetTime());
			}
			else
			{
				HandleLocal(ids, g_Game.GetTime());
			}
		}
	}

	int GetNetworkedIdCount()
	{
		return m_NetworkedIds.Count();
	}

	bool IsAnnouncedNetworked(int batch, int index)
	{
		return m_NetworkedIds.Contains(batch * ID_FACTOR + index);
	}

	int GetPendingSwapCount()
	{
		return m_PendingKeys.Count();
	}

	int GetSwapsDone()
	{
		return m_SwapsDone;
	}

	// twins that exist but are not where their entry says
	int GetTwinMismatchCount()
	{
		return m_TwinMismatch;
	}

	int GetSwapTimeoutCount()
	{
		return m_SwapTimeouts;
	}

	int GetAnnounceDropped()
	{
		return m_AnnounceDropped;
	}

	// valid when the batch exists and the index fits (the real size once the batch is received)
	protected bool ValidId(int batch, int index)
	{
		if (batch < 0 || batch >= m_Batches.Count() || index < 0 || index >= ID_FACTOR)
		{
			return false;
		}
		DZOMS_ClientBatch cb = m_Batches.Get(batch);
		if (cb.data && index >= cb.data.Count())
		{
			return false;
		}
		return true;
	}

	// these objects now have a networked copy: no local copy any more, as soon as the twin is here
	void HandleNetworked(DZOMS_IdList ids, int nowMs)
	{
		if (!ids || !ids.IsValid() || !ids.HasNetIds())
		{
			return;
		}
		for (int n = 0; n < ids.Count(); n++)
		{
			int batch = ids.batch.Get(n);
			int index = ids.index.Get(n);
			if (!ValidId(batch, index))
			{
				continue;
			}
			DZOMS_ClientBatch cb = m_Batches.Get(batch);
			int key = batch * ID_FACTOR + index;
			if (cb.data && cb.data.IsNetworked(index))
			{
				continue;
			}
			if (m_NetworkedIds.Contains(key))
			{
				continue;
			}
			if (m_NetworkedIds.Count() >= m_MaxAnnounced)
			{
				m_AnnounceDropped++;
				continue;
			}
			m_NetworkedIds.Set(key, true);
			int queued = m_RespawnKeys.Find(key);
			if (queued >= 0)
			{
				m_RespawnKeys.RemoveOrdered(queued);
			}
			if (cb.data && cb.objects.Get(index))
			{
				m_PendingKeys.Insert(key);
				m_PendingSince.Insert(nowMs);
				m_PendingLow.Insert(ids.netLow.Get(n));
				m_PendingHigh.Insert(ids.netHigh.Get(n));
			}
		}
	}

	// these objects are local again: spawn the local copy where the batch is spawned
	void HandleLocal(DZOMS_IdList ids, int nowMs)
	{
		if (!ids || !ids.IsValid())
		{
			return;
		}
		for (int n = 0; n < ids.Count(); n++)
		{
			int batch = ids.batch.Get(n);
			int index = ids.index.Get(n);
			if (!ValidId(batch, index))
			{
				continue;
			}
			int key = batch * ID_FACTOR + index;
			if (!m_NetworkedIds.Contains(key))
			{
				continue;
			}
			m_NetworkedIds.Remove(key);
			int pending = m_PendingKeys.Find(key);
			if (pending >= 0)
			{
				m_PendingKeys.RemoveOrdered(pending);
				m_PendingSince.RemoveOrdered(pending);
				m_PendingLow.RemoveOrdered(pending);
				m_PendingHigh.RemoveOrdered(pending);
			}
			DZOMS_ClientBatch cb = m_Batches.Get(batch);
			if (!cb.data || cb.objects.Get(index))
			{
				continue;
			}
			bool spawned = cb.state == DZOMS_ClientBatch.LOADED || (cb.state == DZOMS_ClientBatch.LOADING && cb.cursor > index);
			if (spawned)
			{
				m_RespawnKeys.Insert(key);
			}
		}
	}

	// pending swaps: delete the local copy once the networked twin exists, give up after SWAP_TIMEOUT_MS
	protected void SwapStep(int nowMs)
	{
		int budget = Math.Min(SWAPS_PER_FRAME, m_PendingKeys.Count());
		while (budget > 0 && m_PendingKeys.Count() > 0)
		{
			budget--;
			int key = m_PendingKeys.Get(0);
			int since = m_PendingSince.Get(0);
			int twinLow = m_PendingLow.Get(0);
			int twinHigh = m_PendingHigh.Get(0);
			m_PendingKeys.RemoveOrdered(0);
			m_PendingSince.RemoveOrdered(0);
			m_PendingLow.RemoveOrdered(0);
			m_PendingHigh.RemoveOrdered(0);
			int batch = key / ID_FACTOR;
			int index = key - batch * ID_FACTOR;
			DZOMS_ClientBatch cb = m_Batches.Get(batch);
			Object copy = cb.objects.Get(index);
			if (!copy)
			{
				continue;
			}
			if (TwinPresent(twinLow, twinHigh, cb.data.GetType(index), cb.data.positions.Get(index), cb.data.orientations.Get(index), copy))
			{
				g_Game.ObjectDelete(copy);
				cb.objects.Set(index, null);
				m_SwapsDone++;
				continue;
			}
			if (nowMs - since >= SWAP_TIMEOUT_MS)
			{
				m_SwapTimeouts++;
				continue;
			}
			m_PendingKeys.Insert(key);
			m_PendingSince.Insert(since);
			m_PendingLow.Insert(twinLow);
			m_PendingHigh.Insert(twinHigh);
		}
	}

	// the announced twin: the object with that network id, of the entry's type, at the entry's position.
	// Not a search by type and position: a hidden world object at the same place would pass that.
	protected bool TwinPresent(int low, int high, string type, vector pos, vector ori, Object copy)
	{
		Object twin = g_Game.GetObjectByNetworkId(low, high);
		if (!twin || twin == copy)
		{
			return false;
		}
		string want = DZOMS_TypeName.Of(type);
		if (DZOMS_TypeName.Of(twin.GetType()) != want && DZOMS_TypeName.Of(twin.GetShapeName()) != want)
		{
			return false;
		}
		float off = vector.Distance(twin.GetPosition(), pos);
		if (off > 0.05)
		{
			m_TwinMismatch++;
			int logged = 0;
			m_TwinLogged.Find(type, logged);
			if (logged < 3)
			{
				m_TwinLogged.Set(type, logged + 1);
				DZOMS_Log.Warn("swap: twin of " + type + " is " + off.ToString() + " m from its entry, twin " + twin.GetPosition().ToString());
				DZOMS_Log.Warn("swap: entry " + pos.ToString() + " ori " + ori.ToString() + ", twin ori " + twin.GetOrientation().ToString());
			}
			return false;
		}
		return true;
	}

	protected void RespawnStep()
	{
		int budget = RESPAWNS_PER_FRAME;
		while (budget > 0 && m_RespawnKeys.Count() > 0)
		{
			int key = m_RespawnKeys.Get(0);
			m_RespawnKeys.RemoveOrdered(0);
			int batch = key / ID_FACTOR;
			int index = key - batch * ID_FACTOR;
			DZOMS_ClientBatch cb = m_Batches.Get(batch);
			if (!cb.data || cb.objects.Get(index) || m_NetworkedIds.Contains(key))
			{
				continue;
			}
			if (cb.state != DZOMS_ClientBatch.LOADED && cb.state != DZOMS_ClientBatch.LOADING)
			{
				continue;
			}
			string type = cb.data.GetType(index);
			if (m_FailedTypes.Contains(type))
			{
				continue;
			}
			budget--;
			Object o = SpawnLocal(type, cb.data.positions.Get(index), cb.data.orientations.Get(index), cb.data.scales.Get(index));
			if (o)
			{
				cb.objects.Set(index, o);
			}
			else
			{
				MarkFailed(type);
			}
		}
	}

	void HandleManifest(DZOMS_Manifest m, int nowMs)
	{
		if (m_Manifest)
		{
			return;
		}
		if (!m.IsValid())
		{
			DZOMS_Log.Warn("invalid manifest ignored");
			return;
		}
		if (m.protocolVersion != DZOMS_Rpc.PROTOCOL_VERSION)
		{
			DZOMS_Log.Warn("manifest has protocol " + m.protocolVersion.ToString() + ", this client speaks " + DZOMS_Rpc.PROTOCOL_VERSION.ToString());
			return;
		}
		m_Manifest = m;

		if (m_CachePath == "")
		{
			m_CachePath = CachePathForHost();
		}
		if (!m_CacheLoaded && m_CachePath != "" && !m.disableCache)
		{
			LoadCache();
		}

		for (int i = 0; i < m.Count(); i++)
		{
			DZOMS_ClientBatch cb = new DZOMS_ClientBatch();
			cb.center = m.GetCenter(i);
			cb.radius = m.GetRadius(i);
			cb.state = DZOMS_ClientBatch.NOT_RECEIVED;
			m_Batches.Insert(cb);
		}

		if (m.disableCache)
		{
			if (m_CachePath != "" && FileExist(m_CachePath))
			{
				DeleteFile(m_CachePath);
			}
		}
		else
		{
			for (int j = 0; j < m.Count(); j++)
			{
				string hash = m.hashes.Get(j);
				DZOMS_Batch cached;
				if (m_Cache.Find(hash, cached))
				{
					cached.index = j;
					m_Batches.Get(j).SetData(cached);
					m_Cache.Remove(hash);
					m_Received++;
					m_FromCache++;
				}
			}
		}
		m_Cache.Clear();

		DZOMS_Log.Info("manifest: " + m.Count().ToString() + " batches, " + m_FromCache.ToString() + " from the cache");
		if (m_Received >= m.Count())
		{
			Complete();
			return;
		}
		RequestNext(nowMs);
	}

	void HandleBatch(DZOMS_Batch b, int nowMs)
	{
		if (!m_Manifest || b.index < 0 || b.index >= m_Batches.Count())
		{
			return;
		}
		int index = b.index;
		DZOMS_ClientBatch cb = m_Batches.Get(index);
		if (cb.state != DZOMS_ClientBatch.NOT_RECEIVED)
		{
			return;
		}

		bool good = b.IsValid();
		if (good)
		{
			string hash = DZOMS_BatchHasher.Hash(b);
			good = hash == m_Manifest.hashes.Get(index);
		}
		if (!good)
		{
			DZOMS_Log.Warn("batch " + index.ToString() + " failed validation, dropped");
			if (index == m_InFlight)
			{
				RequestBatch(index, nowMs);
			}
			return;
		}

		cb.SetData(b);
		m_Received++;
		m_Downloaded++;
		if (index == m_InFlight)
		{
			m_InFlight = -1;
			m_Timeouts = 0;
		}
		if (m_Received >= m_Batches.Count())
		{
			Complete();
		}
		else if (m_InFlight < 0 && !m_Paused)
		{
			RequestNext(nowMs);
		}
	}

	protected void Complete()
	{
		m_Complete = true;
		m_InFlight = -1;
		DZOMS_Log.Info("all " + m_Batches.Count().ToString() + " batches received (" + m_Downloaded.ToString() + " downloaded, " + m_FromCache.ToString() + " from the cache)");

		if (m_Manifest.disableCache || m_Downloaded == 0 || m_CachePath == "")
		{
			return;
		}
		array<ref DZOMS_Batch> list = new array<ref DZOMS_Batch>();
		for (int i = 0; i < m_Batches.Count(); i++)
		{
			list.Insert(m_Batches.Get(i).data);
		}
		DZOMS_Config.MakeDirs(DZOMS_Config.PROFILE_DIR);
		MakeDirectory(DZOMS_Config.PROFILE_DIR + "/cache");
		if (DZOMS_Cache.Write(m_CachePath, list))
		{
			DZOMS_Log.Info("cache: wrote " + m_CachePath);
		}
		else
		{
			DZOMS_Log.Warn("cache: could not write " + m_CachePath);
		}
	}
}
