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
		MakeDirectory(DZOMS_Config.PROFILE_DIR);
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
