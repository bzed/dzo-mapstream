// What the server knows about a player for proximity networking (DESIGN 13.3). The prototype uses
// pos and alive only.
class DZOMS_PlayerState
{
	int id;
	vector pos;
	float speed;
	bool inVehicle;
	float height;
	bool alive;
}

// The settings of the zone rules (DESIGN 13.3, 13.7), from the config
class DZOMS_ProximityConfig
{
	float networkRadius;
	float vehicleRadius;
	float maxFootSpeed;
	float maxVehicleSpeed;
	float maxHeight;
	int maxNetworked;

	static DZOMS_ProximityConfig FromConfig(DZOMS_Config c)
	{
		DZOMS_ProximityConfig p = new DZOMS_ProximityConfig();
		p.networkRadius = c.networkRadius;
		p.vehicleRadius = c.vehicleRadius;
		p.maxFootSpeed = c.maxFootSpeed;
		p.maxVehicleSpeed = c.maxVehicleSpeed;
		p.maxHeight = c.maxHeight;
		p.maxNetworked = c.maxNetworked;
		return p;
	}
}

// What a player needs: nothing, or a disc of this radius around them
class DZOMS_Zone
{
	bool active;
	float radius;
}

// Swaps local map objects for networked copies near players and back (DESIGN 13.4). Time is passed
// in, never read from the engine, so tests can drive it.
class DZOMS_Proximity
{
	static const float HYSTERESIS_RATIO = 0.5;	// of the radius
	static const float CELL = 32;
	static const int UPDATE_INTERVAL_MS = 1000;
	static const int RELEASE_DELAY_MS = 30000;
	static const int ACTIVATE_DELAY_MS = 2000;
	static const int STATUS_INTERVAL_MS = 60000;
	static const int RETRY_DELAY_MS = 60000;
	static const int CAP_WARN_INTERVAL_MS = 60000;
	static const int SWAP_DELAY_MS = 500;
	static const int OLD_COPY_DELAY_MS = 5000;	// the server keeps the old copy this long (path graph, DESIGN 13.10e)
	static const int CREATES_PER_FRAME = 5;
	static const int DELETES_PER_FRAME = 10;

	protected DZOMS_ServerWorld m_World;
	protected ref DZOMS_ProximityConfig m_Cfg = new DZOMS_ProximityConfig();
	int m_ActivateDelayMs = ACTIVATE_DELAY_MS;	// tests set it to 0
	protected ref map<int, int> m_Since = new map<int, int>();	// player id -> since when the player qualifies for a zone
	protected ref array<int> m_RetryAt = new array<int>();	// per object: no new attempt before this time
	protected int m_CapHits;
	protected int m_LastCapWarn;
	protected ref map<int, ref array<int>> m_Cells = new map<int, ref array<int>>();
	protected ref array<int> m_NeededAt = new array<int>();	// per object: last time a zone needed it
	protected ref array<int> m_Stamp = new array<int>();	// per object: update that queued it
	protected ref array<int> m_Networked = new array<int>();	// indices of the promoted objects
	protected ref array<int> m_PromoteQueue = new array<int>();
	protected ref array<int> m_DemoteQueue = new array<int>();
	protected int m_PromoteHead;
	protected int m_DemoteHead;
	protected ref array<int> m_AnnounceIdx = new array<int>();	// promoted, not yet announced
	protected ref array<int> m_AnnounceDue = new array<int>();
	protected ref array<Object> m_DeleteObjects = new array<Object>();	// demoted networked copies, to be deleted
	protected ref array<int> m_DeleteDue = new array<int>();
	protected int m_LastUpdate;
	protected bool m_Started;
	protected int m_UpdateCount;
	protected int m_Promotions;
	protected int m_Demotions;
	protected int m_Failures;
	protected int m_UsedUs;
	protected ref map<string, int> m_PromotedByType = new map<string, int>();	// since start: input for proximityExclude
	protected int m_ReportAt;
	protected int m_ReportPromotions;
	protected int m_ReportDemotions;
	protected int m_ReportFailures;

	void Init(DZOMS_ServerWorld world)
	{
		m_World = world;
		m_Cfg = DZOMS_ProximityConfig.FromConfig(world.GetConfig());
		m_Cells.Clear();
		m_NeededAt.Clear();
		m_Stamp.Clear();
		m_RetryAt.Clear();
		m_Networked.Clear();
		m_Since.Clear();
		int count = world.GetObjectCount();
		array<ref DZOMS_ObjectEntry> entries = world.GetObjects();
		array<Object> spawned = world.GetSpawned();
		array<string> exclude = new array<string>();
		for (int x = 0; x < world.GetConfig().proximityExclude.Count(); x++)
		{
			string pattern = world.GetConfig().proximityExclude.Get(x);
			pattern.ToLower();
			exclude.Insert(pattern);
		}
		map<string, bool> excludedTypes = new map<string, bool>();
		for (int i = 0; i < count; i++)
		{
			m_NeededAt.Insert(0);
			m_Stamp.Insert(0);
			m_RetryAt.Insert(0);
			if ((entries.Get(i).flags & DZOMS_Batch.FLAG_NETWORKED) == DZOMS_Batch.FLAG_NETWORKED || !spawned.Get(i))
			{
				continue;
			}
			if (exclude.Count() > 0)
			{
				string typeName = DZOMS_TypeName.Of(entries.Get(i).type);
				bool excluded = false;
				if (!excludedTypes.Find(typeName, excluded))
				{
					for (int e = 0; e < exclude.Count(); e++)
					{
						if (DZOMS_Glob.Match(exclude.Get(e), typeName))
						{
							excluded = true;
							break;
						}
					}
					excludedTypes.Set(typeName, excluded);
				}
				if (excluded)
				{
					continue;
				}
			}
			int key = CellKey(entries.Get(i).pos);
			array<int> cell = m_Cells.Get(key);
			if (!cell)
			{
				cell = new array<int>();
				m_Cells.Set(key, cell);
			}
			cell.Insert(i);
		}
	}

	protected static int CellKey(vector p)
	{
		return CellKeyXZ(Math.Floor(p[0] / CELL), Math.Floor(p[2] / CELL));
	}

	protected static int CellKeyXZ(int cx, int cz)
	{
		return (cx + 2048) * 4096 + (cz + 2048);
	}

	DZOMS_ProximityConfig GetConfig()
	{
		return m_Cfg;
	}

	int GetCapHits()
	{
		return m_CapHits;
	}

	// what a player needs (DESIGN 13.3): no zone when dead, too high, or too fast for what they are in
	static DZOMS_Zone ZoneFor(DZOMS_PlayerState p, DZOMS_ProximityConfig c)
	{
		DZOMS_Zone z = new DZOMS_Zone();
		if (!p.alive || p.height > c.maxHeight)
		{
			return z;
		}
		if (p.inVehicle)
		{
			if (p.speed <= c.maxVehicleSpeed)
			{
				z.active = true;
				z.radius = c.vehicleRadius;
			}
			return z;
		}
		if (p.speed <= c.maxFootSpeed)
		{
			z.active = true;
			z.radius = c.networkRadius;
		}
		return z;
	}

	// microseconds spent in Update and ProcessQueue, measured by the caller
	void AddUsedUs(int us)
	{
		m_UsedUs += us;
	}

	// one short line a minute while anything happened, else ""
	string StatusLine(int nowMs)
	{
		if (nowMs - m_ReportAt < STATUS_INTERVAL_MS)
		{
			return "";
		}
		int promotions = m_Promotions - m_ReportPromotions;
		int demotions = m_Demotions - m_ReportDemotions;
		int failures = m_Failures - m_ReportFailures;
		int usedMs = m_UsedUs / 1000;
		m_ReportAt = nowMs;
		m_ReportPromotions = m_Promotions;
		m_ReportDemotions = m_Demotions;
		m_ReportFailures = m_Failures;
		m_UsedUs = 0;
		if (promotions == 0 && demotions == 0 && failures == 0 && GetQueueLength() == 0)
		{
			return "";
		}
		return "proximity: " + m_Networked.Count().ToString() + " of at most " + m_Cfg.maxNetworked.ToString() + " networked, queue " + GetQueueLength().ToString() + ", +" + promotions.ToString() + " -" + demotions.ToString() + " failed " + failures.ToString() + " in the last minute, " + usedMs.ToString() + " ms used";
	}

	// the types promoted most often since start, one short line each (to decide on proximityExclude)
	array<string> TypeReport(int top)
	{
		array<string> lines = new array<string>();
		array<int> counts = new array<int>();
		array<string> names = new array<string>();
		for (int i = 0; i < m_PromotedByType.Count(); i++)
		{
			counts.Insert(m_PromotedByType.GetElement(i));
			names.Insert(m_PromotedByType.GetKey(i));
		}
		for (int t = 0; t < top; t++)
		{
			int best = -1;
			for (int j = 0; j < counts.Count(); j++)
			{
				if (counts.Get(j) > 0 && (best < 0 || counts.Get(j) > counts.Get(best)))
				{
					best = j;
				}
			}
			if (best < 0)
			{
				break;
			}
			lines.Insert("proximity: promoted " + counts.Get(best).ToString() + "x " + names.Get(best));
			counts.Set(best, 0);
		}
		return lines;
	}

	bool NeedsUpdate(int nowMs)
	{
		return !m_Started || nowMs - m_LastUpdate >= UPDATE_INTERVAL_MS;
	}

	int GetNetworkedCount()
	{
		return m_Networked.Count();
	}

	int GetQueueLength()
	{
		return (m_PromoteQueue.Count() - m_PromoteHead) + (m_DemoteQueue.Count() - m_DemoteHead);
	}

	int GetPromotions()
	{
		return m_Promotions;
	}

	int GetDemotions()
	{
		return m_Demotions;
	}

	int GetFailures()
	{
		return m_Failures;
	}

	// builds the zones, queues promotions (nearest first) and demotions (farthest first); once per UPDATE_INTERVAL_MS
	void Update(array<ref DZOMS_PlayerState> players, int nowMs)
	{
		if (!m_World || (m_Started && nowMs - m_LastUpdate < UPDATE_INTERVAL_MS))
		{
			return;
		}
		m_Started = true;
		m_LastUpdate = nowMs;
		m_UpdateCount++;

		array<vector> zones = new array<vector>();
		array<float> radii = new array<float>();
		map<int, int> seenNow = new map<int, int>();
		for (int p = 0; p < players.Count(); p++)
		{
			DZOMS_PlayerState ps = players.Get(p);
			if (!ps)
			{
				continue;
			}
			DZOMS_Zone zone = ZoneFor(ps, m_Cfg);
			if (!zone.active)
			{
				continue;
			}
			// a zone opens after the player has qualified for it for a while
			int since = nowMs;
			if (!m_Since.Find(ps.id, since))
			{
				since = nowMs;
			}
			seenNow.Set(ps.id, since);
			if (nowMs - since >= m_ActivateDelayMs)
			{
				zones.Insert(ps.pos);
				radii.Insert(zone.radius);
			}
		}
		m_Since = seenNow;

		int count = m_World.GetObjectCount();
		array<ref DZOMS_ObjectEntry> entries = m_World.GetObjects();
		array<int> keys = new array<int>();
		for (int z = 0; z < zones.Count(); z++)
		{
			vector zc = zones.Get(z);
			float zr = radii.Get(z);
			int x0 = Math.Floor((zc[0] - zr) / CELL);
			int x1 = Math.Floor((zc[0] + zr) / CELL);
			int z0 = Math.Floor((zc[2] - zr) / CELL);
			int z1 = Math.Floor((zc[2] + zr) / CELL);
			for (int cx = x0; cx <= x1; cx++)
			{
				for (int cz = z0; cz <= z1; cz++)
				{
					array<int> cell = m_Cells.Get(CellKeyXZ(cx, cz));
					if (!cell)
					{
						continue;
					}
					for (int c = 0; c < cell.Count(); c++)
					{
						int idx = cell.Get(c);
						if (m_World.IsPromoted(idx) || m_RetryAt.Get(idx) > nowMs)
						{
							continue;
						}
						float d = vector.Distance(zc, entries.Get(idx).pos);
						if (d <= zr)
						{
							keys.Insert(Math.Round(d * 10) * (count + 1) + idx);
						}
					}
				}
			}
		}
		keys.Sort();
		m_PromoteQueue.Clear();
		m_PromoteHead = 0;
		int room = Math.Max(0, m_Cfg.maxNetworked - m_Networked.Count());
		bool capped = false;
		for (int k = 0; k < keys.Count(); k++)
		{
			int qi = keys.Get(k) % (count + 1);
			if (m_Stamp.Get(qi) == m_UpdateCount)
			{
				continue;
			}
			if (m_PromoteQueue.Count() >= room)
			{
				capped = true;
				break;
			}
			m_Stamp.Set(qi, m_UpdateCount);
			m_PromoteQueue.Insert(qi);
		}
		if (capped)
		{
			m_CapHits++;
			if (m_CapHits == 1 || nowMs - m_LastCapWarn >= CAP_WARN_INTERVAL_MS)
			{
				m_LastCapWarn = nowMs;
				DZOMS_Log.Warn("proximity: " + m_Networked.Count().ToString() + " objects are networked, the limit maxNetworked is " + m_Cfg.maxNetworked.ToString() + ": the nearest ones win");
			}
		}

		array<int> demote = new array<int>();
		for (int n = 0; n < m_Networked.Count(); n++)
		{
			int ni = m_Networked.Get(n);
			// distance in units of the zone's radius: needed up to 1, released beyond 1 + HYSTERESIS_RATIO
			float ratio = 99999;
			for (int zz = 0; zz < zones.Count(); zz++)
			{
				ratio = Math.Min(ratio, vector.Distance(zones.Get(zz), entries.Get(ni).pos) / radii.Get(zz));
			}
			if (ratio <= 1)
			{
				m_NeededAt.Set(ni, nowMs);
			}
			else if (ratio > 1 + HYSTERESIS_RATIO && nowMs - m_NeededAt.Get(ni) >= RELEASE_DELAY_MS && m_RetryAt.Get(ni) <= nowMs)
			{
				demote.Insert((100000 - Math.Round(Math.Min(ratio, 999) * 100)) * (count + 1) + ni);
			}
		}
		demote.Sort();
		m_DemoteQueue.Clear();
		m_DemoteHead = 0;
		for (int dq = 0; dq < demote.Count(); dq++)
		{
			m_DemoteQueue.Insert(demote.Get(dq) % (count + 1));
		}
	}

	// per frame: due announcements and deletions, then at most CREATES_PER_FRAME promotions and
	// DELETES_PER_FRAME demotions
	void ProcessQueue(int nowMs)
	{
		DZOMS_IdList due = new DZOMS_IdList();
		while (m_AnnounceIdx.Count() > 0 && m_AnnounceDue.Get(0) <= nowMs)
		{
			int ai = m_AnnounceIdx.Get(0);
			int netLow, netHigh;
			if (m_World.GetNetworkId(ai, netLow, netHigh))
			{
				due.AddNet(m_World.GetObjectBatch(ai), m_World.GetObjectIndexInBatch(ai), netLow, netHigh);
			}
			m_AnnounceIdx.RemoveOrdered(0);
			m_AnnounceDue.RemoveOrdered(0);
		}
		if (due.Count() > 0)
		{
			m_World.BroadcastNetworked(due);
		}

		while (m_DeleteObjects.Count() > 0 && m_DeleteDue.Get(0) <= nowMs)
		{
			Object doomed = m_DeleteObjects.Get(0);
			if (doomed)
			{
				g_Game.ObjectDelete(doomed);
			}
			m_DeleteObjects.RemoveOrdered(0);
			m_DeleteDue.RemoveOrdered(0);
		}

		bool changed = false;
		int creates = CREATES_PER_FRAME;
		while (creates > 0 && m_PromoteHead < m_PromoteQueue.Count())
		{
			int pi = m_PromoteQueue.Get(m_PromoteHead);
			m_PromoteHead++;
			if (m_World.IsPromoted(pi))
			{
				continue;
			}
			creates--;
			Object oldLocal;
			if (m_World.PromoteKeepOld(pi, oldLocal))
			{
				m_DeleteObjects.Insert(oldLocal);
				m_DeleteDue.Insert(nowMs + OLD_COPY_DELAY_MS);
				m_Networked.Insert(pi);
				m_NeededAt.Set(pi, nowMs);
				m_AnnounceIdx.Insert(pi);
				m_AnnounceDue.Insert(nowMs + SWAP_DELAY_MS);
				m_Promotions++;
				string promotedType = DZOMS_TypeName.Of(m_World.GetObjects().Get(pi).type);
				int promotedBefore = 0;
				m_PromotedByType.Find(promotedType, promotedBefore);
				m_PromotedByType.Set(promotedType, promotedBefore + 1);
				changed = true;
			}
			else
			{
				m_Failures++;
				m_RetryAt.Set(pi, nowMs + RETRY_DELAY_MS);
			}
		}

		DZOMS_IdList toLocal = new DZOMS_IdList();
		int deletes = DELETES_PER_FRAME;
		while (deletes > 0 && m_DemoteHead < m_DemoteQueue.Count())
		{
			int di = m_DemoteQueue.Get(m_DemoteHead);
			m_DemoteHead++;
			if (!m_World.IsPromoted(di))
			{
				continue;
			}
			deletes--;
			Object oldNetworked;
			if (m_World.Demote(di, oldNetworked))
			{
				int at = m_Networked.Find(di);
				if (at >= 0)
				{
					m_Networked.RemoveOrdered(at);
				}
				int pending = m_AnnounceIdx.Find(di);
				if (pending >= 0)
				{
					m_AnnounceIdx.RemoveOrdered(pending);
					m_AnnounceDue.RemoveOrdered(pending);
				}
				toLocal.Add(m_World.GetObjectBatch(di), m_World.GetObjectIndexInBatch(di));
				m_DeleteObjects.Insert(oldNetworked);
				m_DeleteDue.Insert(nowMs + OLD_COPY_DELAY_MS);
				m_Demotions++;
				changed = true;
			}
			else
			{
				m_Failures++;
				m_RetryAt.Set(di, nowMs + RETRY_DELAY_MS);
			}
		}
		if (toLocal.Count() > 0)
		{
			m_World.BroadcastLocal(toLocal);
		}
		if (changed)
		{
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
		}
	}
}
