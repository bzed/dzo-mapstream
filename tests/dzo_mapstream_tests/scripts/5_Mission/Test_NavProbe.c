// Probes of the AI navigation mesh (zombies): can a path cross a wall of mod objects? Scenarios
// "navscan" (find a good open spot) and "navprobe" (timeline of one wall under a variant of the swap).
class DZOMS_NavProbe: Managed
{
	static ref DZOMS_NavProbe s_Run;

	static ref PGFilter s_Filter;
	static ref PGFilter s_JumpFilter;

	// walking only: ground, doors, inside buildings
	static PGFilter MakeFilter()
	{
		if (!s_Filter)
		{
			s_Filter = new PGFilter();
			s_Filter.SetFlags(PGPolyFlags.WALK | PGPolyFlags.DOOR | PGPolyFlags.INSIDE, PGPolyFlags.DISABLED | PGPolyFlags.UNREACHABLE, 0);
		}
		return s_Filter;
	}

	// plus jumping over and down
	static PGFilter MakeJumpFilter()
	{
		if (!s_JumpFilter)
		{
			s_JumpFilter = new PGFilter();
			s_JumpFilter.SetFlags(PGPolyFlags.WALK | PGPolyFlags.DOOR | PGPolyFlags.INSIDE | PGPolyFlags.JUMP_OVER | PGPolyFlags.JUMP_DOWN | PGPolyFlags.CLIMB, PGPolyFlags.DISABLED | PGPolyFlags.UNREACHABLE, 0);
		}
		return s_JumpFilter;
	}

	// path length from a to b over the navmesh, -1 when there is none
	static float PathLength(vector a, vector b, out int waypoints, PGFilter filter)
	{
		AIWorld ai = g_Game.GetWorld().GetAIWorld();
		TVectorArray wp = new TVectorArray();
		waypoints = 0;
		if (!ai.FindPath(a, b, filter, wp))
		{
			return -1;
		}
		waypoints = wp.Count();
		float len = 0;
		for (int i = 1; i < wp.Count(); i++)
		{
			len += vector.Distance(wp.Get(i - 1), wp.Get(i));
		}
		return len;
	}

	static string Probe(vector a, vector b)
	{
		int n;
		float len = PathLength(a, b, n, MakeFilter());
		int nj;
		float lenJump = PathLength(a, b, nj, MakeJumpFilter());
		vector hit;
		vector normal;
		bool ray = g_Game.GetWorld().GetAIWorld().RaycastNavMesh(a, b, MakeFilter(), hit, normal);
		vector from = a + "0 1 0";
		vector to = b + "0 1 0";
		vector contact;
		vector dir;
		int comp;
		set<Object> results = new set<Object>();
		bool phys = DayZPhysics.RaycastRV(from, to, contact, dir, comp, results, null, null, false, false, ObjIntersectGeom, 0.0);
		return "walk=" + len.ToString() + " wp=" + n.ToString() + " withJumps=" + lenJump.ToString() + " wp=" + nj.ToString() + " straight=" + vector.Distance(a, b).ToString() + " navRay=" + ray.ToString() + " physRay=" + phys.ToString();
	}

	static vector OnGround(float x, float z)
	{
		return Vector(x, g_Game.SurfaceY(x, z), z);
	}

	// ---- scan
	static void Scan()
	{
		array<int> flagValues = {PGPolyFlags.WALK, PGPolyFlags.DISABLED, PGPolyFlags.DOOR, PGPolyFlags.INSIDE, PGPolyFlags.SWIM, PGPolyFlags.LADDER, PGPolyFlags.JUMP_OVER, PGPolyFlags.JUMP_DOWN, PGPolyFlags.CLIMB, PGPolyFlags.CRAWL, PGPolyFlags.CROUCH, PGPolyFlags.UNREACHABLE, PGPolyFlags.ALL, PGPolyFlags.JUMP, PGPolyFlags.SPECIAL};
		array<string> flagNames = {"WALK", "DISABLED", "DOOR", "INSIDE", "SWIM", "LADDER", "JUMP_OVER", "JUMP_DOWN", "CLIMB", "CRAWL", "CROUCH", "UNREACHABLE", "ALL", "JUMP", "SPECIAL"};
		string flagLine = "navscan: flags";
		for (int f = 0; f < flagValues.Count(); f++)
		{
			flagLine = flagLine + " " + flagNames.Get(f) + "=" + flagValues.Get(f).ToString();
		}
		DZOMS_Log.Info(flagLine);
		array<float> xs = {4500, 4700, 5100, 8200, 8600, 2700, 3300, 10100, 6000, 7800, 11800, 12200};
		array<float> zs = {10300, 10100, 10300, 6200, 6500, 5300, 5800, 9300, 9000, 11900, 9200, 8800};
		for (int i = 0; i < xs.Count(); i++)
		{
			vector c = OnGround(xs.Get(i), zs.Get(i));
			vector a = OnGround(xs.Get(i) - 12, zs.Get(i));
			vector b = OnGround(xs.Get(i) + 12, zs.Get(i));
			vector sample;
			bool onMesh = g_Game.GetWorld().GetAIWorld().SampleNavmeshPosition(c, 3, MakeFilter(), sample);
			DZOMS_Log.Info("navscan " + xs.Get(i).ToString() + " " + zs.Get(i).ToString() + " y=" + c[1].ToString() + " onMesh=" + onMesh.ToString() + " " + Probe(a, b));
		}
		DZOMS_T.Done();
	}

	// control: does the navmesh of the vanilla map block paths through its buildings?
	static void Control()
	{
		array<float> xs = {4500, 6600, 10300, 8300};
		array<float> zs = {10300, 2500, 2300, 6000};
		int shown = 0;
		for (int i = 0; i < xs.Count(); i++)
		{
			array<Object> found = new array<Object>();
			array<CargoBase> prox = new array<CargoBase>();
			g_Game.GetObjectsAtPosition3D(OnGround(xs.Get(i), zs.Get(i)), 150, found, prox);
			for (int j = 0; j < found.Count() && shown < 14; j++)
			{
				Building b = Building.Cast(found.Get(j));
				if (!b || !b.GetType().Contains("Land_"))
				{
					continue;
				}
				vector c = b.GetPosition();
				vector mm[2];
				float r = b.ClippingInfo(mm);
				if (r < 4 || r > 14)
				{
					continue;
				}
				shown++;
				vector a = Vector(c[0] - r - 2, g_Game.SurfaceY(c[0] - r - 2, c[2]), c[2]);
				vector d = Vector(c[0] + r + 2, g_Game.SurfaceY(c[0] + r + 2, c[2]), c[2]);
				int net;
				bool isNet = false;
				int low, high;
				b.GetNetworkID(low, high);
				DZOMS_Log.Info("navcontrol " + b.GetType() + " r=" + r.ToString() + " netId=" + low.ToString() + " " + Probe(a, d));
			}
		}
		DZOMS_T.Done();
	}

	static void StartControl()
	{
		s_Run = new DZOMS_NavProbe();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(DZOMS_NavProbe.Control, 15000, false);
	}

	static void StartScan()
	{
		s_Run = new DZOMS_NavProbe();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(DZOMS_NavProbe.Scan, 15000, false);
	}

	// ---- timeline of a wall under a variant of the swap
	// wall: 7 containers along z at x0, the probe walks from x0 - 10 to x0 + 10 through the middle
	static const float WALL_X = 4500;
	static const float WALL_Z = 10300;

	static string Variant()
	{
		string line = "";
		FileHandle fh = OpenFile(DZOMS_T.DIR + "/variant.txt", FileMode.READ);
		if (fh != 0)
		{
			FGets(fh, line);
			CloseFile(fh);
		}
		line.TrimInPlace();
		return line;
	}

	static void WriteWall()
	{
		DZOMS_Config.MakeDirs(DZOMS_Config.MAPS_DIR);
		EditorSaveData wall = new EditorSaveData();
		for (int k = -3; k <= 3; k++)
		{
			float z = WALL_Z + k * 10.6;
			vector p = OnGround(WALL_X, z);
			p[1] = p[1] + 1.25;
			DZOMS_TestFixtures.AddObject(wall, "Land_Container_1Bo", p, "0 0 0", 1, 0);
		}
		DZOMS_TestFixtures.Save(wall, DZOMS_Config.MAPS_DIR + "/navwall.dze");
	}

	string m_Variant;
	int m_StartMs;
	int m_SwapMs;
	int m_LastProbe;
	int m_LastUpdate;
	bool m_Swapped;
	ref DZOMS_Proximity m_Prox;
	ref array<ref DZOMS_PlayerState> m_Players = new array<ref DZOMS_PlayerState>();

	static void StartTimeline()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_Variant = Variant();
		s_Run.m_StartMs = g_Game.GetTime();
		DZOMS_Log.Info("navprobe: variant '" + s_Run.m_Variant + "'");
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.Tick, 250, true);
	}

	void Swap()
	{
		DZOMS_ServerWorld w = GetDayZGame().DZOMS_GetServerWorld();
		DZOMS_Log.Info("navprobe: the swap starts, " + w.GetObjectCount().ToString() + " objects, variant " + m_Variant);
		if (m_Variant.Contains("nopath"))
		{
			w.GetConfig().experimentalNoPathgraph = true;
		}
		if (m_Variant.Contains("keep"))
		{
			w.GetConfig().experimentalKeepLocalCopy = true;
		}
		array<Object> spawned = w.GetSpawned();
		if (m_Variant.Contains("mark"))
		{
			for (int i = 0; i < spawned.Count(); i++)
			{
				g_Game.GetWorld().MarkObjectForPathgraphUpdate(spawned.Get(i));
			}
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			DZOMS_Log.Info("navprobe: marked " + spawned.Count().ToString() + " objects and processed");
		}
		if (m_Variant.Contains("region"))
		{
			vector c = OnGround(WALL_X, WALL_Z);
			g_Game.UpdatePathgraphRegion(Vector(c[0] - 60, c[1], c[2] - 60), Vector(c[0] + 60, c[1], c[2] + 60));
			DZOMS_Log.Info("navprobe: region updated");
		}
		if (m_Variant.Contains("entity"))
		{
			for (int e = 0; e < spawned.Count(); e++)
			{
				g_Game.GetWorld().UpdatePathgraphEntityState(spawned.Get(e));
			}
			DZOMS_Log.Info("navprobe: entity state updated");
		}
		if (m_Variant == "none" || m_Variant.Contains("mark") || m_Variant.Contains("region") || m_Variant.Contains("entity"))
		{
			return;
		}
		m_Prox = new DZOMS_Proximity();
		m_Prox.Init(w);
		m_Prox.m_ActivateDelayMs = 0;
		DZOMS_PlayerState p = new DZOMS_PlayerState();
		p.id = "navtest";
		p.pos = OnGround(WALL_X, WALL_Z);
		p.alive = true;
		m_Players.Insert(p);
	}

	void Tick()
	{
		int now = g_Game.GetTime();
		int t = now - m_StartMs;
		if (!m_Swapped && t >= 15000)
		{
			m_Swapped = true;
			m_SwapMs = now;
			Swap();
		}
		if (m_Prox)
		{
			if (m_Prox.NeedsUpdate(now))
			{
				m_Prox.Update(m_Players, now);
			}
			m_Prox.ProcessQueue(now);
		}
		if (now - m_LastProbe >= 1000)
		{
			m_LastProbe = now;
			int net = 0;
			if (m_Prox)
			{
				net = m_Prox.GetNetworkedCount();
			}
			string phase = "before";
			int rel = t;
			if (m_Swapped)
			{
				phase = "after";
				rel = now - m_SwapMs;
			}
			string line = "nav " + phase + " t=" + (rel / 1000).ToString() + " networked=" + net.ToString() + " " + Probe(OnGround(WALL_X - 10, WALL_Z), OnGround(WALL_X + 10, WALL_Z));
			DZOMS_Log.Info(line);
		}
		if (m_Swapped && now - m_SwapMs >= 70000)
		{
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(Tick);
			DZOMS_T.Done();
		}
	}

	// ---- runtime-spawned objects and the navmesh: which way of spawning blocks a path?
	ref array<Object> m_Objs = new array<Object>();
	ref array<string> m_Names = new array<string>();
	ref array<vector> m_From = new array<vector>();
	ref array<vector> m_To = new array<vector>();
	int m_Stage;

	bool m_WaitForPlayer;
	int m_WaitMs;
	vector m_Center;
	int m_PlayerSeenMs;

	static void StartSpawn()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_StartMs = g_Game.GetTime();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.SpawnTick, 500, true);
	}

	// variant bits: 1 = local, 2 = ECE_UPDATEPATHGRAPH, 4 = SetPosition/SetOrientation/Update after creation, 8 = MarkObjectForPathgraphUpdate
	Object SpawnVariant(int v, string type, vector pos)
	{
		int flags = ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR;
		if ((v & 1) != 0)
		{
			flags = flags | ECE_LOCAL;
		}
		if ((v & 2) != 0)
		{
			flags = flags | ECE_UPDATEPATHGRAPH;
		}
		Object o = g_Game.CreateObjectEx(type, pos, flags);
		if (o && (v & 4) != 0)
		{
			o.SetPosition(pos);
			o.SetOrientation("0 0 0");
			o.Update();
		}
		if (o && (v & 8) != 0)
		{
			g_Game.GetWorld().MarkObjectForPathgraphUpdate(o);
		}
		if (o && (v & 16) != 0)
		{
			// the exported init.c of the DayZ Editor
			o.SetAffectPathgraph(true, false);
			if (o.CanAffectPathgraph())
			{
				g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(g_Game.UpdatePathgraphRegionByObject, 100, false, o);
			}
		}
		return o;
	}

	static string VariantName(int v)
	{
		string n = "networked";
		if ((v & 1) != 0)
		{
			n = "local";
		}
		if ((v & 2) != 0)
		{
			n = n + " flag";
		}
		if ((v & 4) != 0)
		{
			n = n + " setpos+update";
		}
		if ((v & 8) != 0)
		{
			n = n + " mark";
		}
		if ((v & 16) != 0)
		{
			n = n + " editor-region";
		}
		return n;
	}

	// open ground: the walk between two points 18 m apart is straight along x and along z
	static bool OpenSite(vector c)
	{
		int n;
		vector hit;
		vector normal;
		for (int axis = 0; axis < 2; axis++)
		{
			vector a = c;
			vector b = c;
			if (axis == 0)
			{
				a[0] = a[0] - 9;
				b[0] = b[0] + 9;
			}
			else
			{
				a[2] = a[2] - 9;
				b[2] = b[2] + 9;
			}
			a[1] = g_Game.SurfaceY(a[0], a[2]);
			b[1] = g_Game.SurfaceY(b[0], b[2]);
			float len = PathLength(a, b, n, MakeFilter());
			if (len < 0 || len > vector.Distance(a, b) * 1.05 || g_Game.GetWorld().GetAIWorld().RaycastNavMesh(a, b, MakeFilter(), hit, normal))
			{
				return false;
			}
		}
		return true;
	}

	// 0 open, 1 walk detour / navmesh ray hit, for the two axes
	static string Blocked(vector c)
	{
		string r = "";
		for (int axis = 0; axis < 2; axis++)
		{
			vector a = c;
			vector b = c;
			if (axis == 0)
			{
				a[0] = a[0] - 9;
				b[0] = b[0] + 9;
			}
			else
			{
				a[2] = a[2] - 9;
				b[2] = b[2] + 9;
			}
			a[1] = g_Game.SurfaceY(a[0], a[2]);
			b[1] = g_Game.SurfaceY(b[0], b[2]);
			int n;
			float len = PathLength(a, b, n, MakeFilter());
			vector hit;
			vector normal;
			bool ray = g_Game.GetWorld().GetAIWorld().RaycastNavMesh(a, b, MakeFilter(), hit, normal);
			if (ray || len < 0 || len > vector.Distance(a, b) * 1.05)
			{
				r = r + "BLOCKED(" + len.ToString() + ") ";
			}
			else
			{
				r = r + "open ";
			}
		}
		return r;
	}

	ref array<vector> m_Sites = new array<vector>();
	ref array<Object> m_Zombies = new array<Object>();

	static void StartClientSpawn()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_StartMs = g_Game.GetTime();
		s_Run.m_WaitForPlayer = true;
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.SpawnTick, 500, true);
	}

	void SpawnTick()
	{
		int now = g_Game.GetTime();
		int t = now - m_StartMs;
		if (m_WaitForPlayer && m_Stage == 0)
		{
			array<Man> men = new array<Man>();
			g_Game.GetPlayers(men);
			if (men.Count() == 0)
			{
				return;
			}
			if (m_PlayerSeenMs == 0)
			{
				m_PlayerSeenMs = now;
				string mode = Variant();
				m_WaitMs = 25000;
				if (mode.Contains("wait60"))
				{
					m_WaitMs = 60000;
				}
				if (mode.Contains("natural"))
				{
					m_Center = men.Get(0).GetPosition();
				}
				else
				{
					m_Center = OnGround(4410, 10210);
					men.Get(0).SetPosition(m_Center);
				}
				DZOMS_Log.Info("navspawn: a player is here (" + mode + "), centre " + m_Center.ToString() + ", waiting " + m_WaitMs.ToString() + " ms");
			}
			if (now - m_PlayerSeenMs < m_WaitMs)
			{
				return;
			}
			t = 99999;
		}
		array<string> types = {"Land_Container_1Bo", "StaticObj_Wall_IndCnch_10"};
		array<int> variants = {2, 8, 2 + 4, 4 + 8, 1 + 2, 1 + 8, 1 + 2 + 4, 1 + 4 + 8};
		if (Variant().Contains("editor"))
		{
			variants = {4 + 16, 2 + 4 + 16, 1 + 4 + 16, 1 + 2 + 4 + 16, 16, 2 + 16, 4 + 8 + 16, 1 + 16};
		}
		int spawnAt = 12000;
		if (Variant().Contains("late"))
		{
			spawnAt = 200000;
		}
		if (m_Stage == 0 && t >= spawnAt)
		{
			m_Stage = 1;
			for (int i = 0; i < 8; i++)
			{
				for (int j = 0; j < 8; j++)
				{
					vector c = OnGround(4200 + 60 * i, 10000 + 60 * j);
					if (m_WaitForPlayer)
					{
						c = OnGround(m_Center[0] - 240 + 70 * i, m_Center[2] - 240 + 70 * j);
					}
					if (OpenSite(c))
					{
						m_Sites.Insert(c);
					}
				}
			}
			DZOMS_Log.Info("navspawn: " + m_Sites.Count().ToString() + " open sites of 64");
			int site = 0;
			for (int ty = 0; ty < types.Count(); ty++)
			{
				for (int v = 0; v < variants.Count(); v++)
				{
					if (site >= m_Sites.Count())
					{
						break;
					}
					vector pos = m_Sites.Get(site);
					site++;
					Object o = SpawnVariant(variants.Get(v), types.Get(ty), pos);
					if (!o)
					{
						continue;
					}
					m_Objs.Insert(o);
					m_Names.Insert(types.Get(ty) + " " + VariantName(variants.Get(v)));
					m_From.Insert(pos);
					m_To.Insert(pos);
				}
			}
			// vanilla's own runtime obstacle: a pitched tent (TentBase.RegenerateNavmesh)
			if (site < m_Sites.Count())
			{
				TentBase tent = TentBase.Cast(g_Game.CreateObjectEx("LargeTent", m_Sites.Get(site), ECE_PLACE_ON_SURFACE));
				if (tent)
				{
					tent.Pitch(true, true);
					m_Objs.Insert(tent);
					m_Names.Insert("vanilla LargeTent pitched");
					m_From.Insert(m_Sites.Get(site));
					m_To.Insert(m_Sites.Get(site));
				}
				site++;
			}
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			m_SwapMs = now;
			DZOMS_Log.Info("navspawn: spawned " + m_Objs.Count().ToString() + " objects, processed");
		}
		if (m_Stage >= 1 && now - m_LastProbe >= 6000)
		{
			m_LastProbe = now;
			int rel = (now - m_SwapMs) / 1000;
			for (int k = 0; k < m_Objs.Count(); k++)
			{
				DZOMS_Log.Info("navspawn t=" + rel.ToString() + " " + m_Names.Get(k) + " : " + Blocked(m_From.Get(k)));
			}
		}
		if (m_Stage == 1 && now - m_SwapMs >= 10000)
		{
			// an AI agent near each object: does the navmesh around it start to take the objects in?
			m_Stage = 2;
			for (int z = 0; z < m_Objs.Count(); z++)
			{
				vector zp = m_From.Get(z) + "25 0 0";
				zp[1] = g_Game.SurfaceY(zp[0], zp[2]);
				Object zombie = g_Game.CreateObjectEx("ZmbM_HermitSkinny_Beige", zp, ECE_PLACE_ON_SURFACE);
				if (zombie)
				{
					m_Zombies.Insert(zombie);
				}
			}
			DZOMS_Log.Info("navspawn: " + m_Zombies.Count().ToString() + " zombies spawned");
			// and a player (no connection) at every second site
			int made = 0;
			for (int q = 0; q < m_Objs.Count(); q = q + 1)
			{
				vector pp = m_From.Get(q) + "-15 0 0";
				pp[1] = g_Game.SurfaceY(pp[0], pp[2]);
				Man man = Man.Cast(g_Game.CreatePlayer(null, "SurvivorM_Mirek", pp, 0, "NONE"));
				if (man)
				{
					made++;
				}
			}
			DZOMS_Log.Info("navspawn: " + made.ToString() + " test players created");
		}
		int endMs = 45000;
		if (Variant().Contains("xlong"))
		{
			endMs = 330000;
		}
		else if (Variant().Contains("long"))
		{
			endMs = 150000;
		}
		if (m_Stage >= 1 && now - m_SwapMs >= endMs)
		{
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(SpawnTick);
			DZOMS_T.Done();
		}
	}

	// ---- swap: a local object that is already an obstacle is replaced by a networked copy; for how long is the place open?
	ref array<Object> m_SwapNew = new array<Object>();
	ref array<int> m_SwapDelete = new array<int>();
	int m_SwapAt;

	static void StartSwap()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_StartMs = g_Game.GetTime();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.SwapTick, 500, true);
	}

	void SwapTick()
	{
		int now = g_Game.GetTime();
		int t = now - m_StartMs;
		array<int> delays = {0, 20000, 45000, 90000, -1, 0, -1, 0};
		if (m_Stage == 0 && t >= 12000)
		{
			m_Stage = 1;
			for (int i = 0; i < 8; i++)
			{
				vector c = OnGround(4200 + 60 * i, 10000 + 60 * 3);
				if (!OpenSite(c))
				{
					c = OnGround(4200 + 60 * i, 10000 + 60 * 5);
				}
				Object o = g_Game.CreateObjectEx("Land_Container_1Bo", c, ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR | ECE_LOCAL | ECE_UPDATEPATHGRAPH);
				m_Objs.Insert(o);
				m_From.Insert(c);
				m_SwapNew.Insert(null);
				m_SwapDelete.Insert(0);
			}
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			DZOMS_Log.Info("navswap: " + m_Objs.Count().ToString() + " local containers");
		}
		if (m_Stage == 1 && m_PlayerSeenMs == 0 && now - m_LastProbe >= 2000)
		{
			m_LastProbe = now;
			if (Blocked(m_From.Get(0)).Contains("BLOCKED"))
			{
				m_PlayerSeenMs = now;
				DZOMS_Log.Info("navswap: obstacles registered at t=" + (t / 1000).ToString());
			}
		}
		if (m_Stage == 1 && m_PlayerSeenMs != 0 && now - m_PlayerSeenMs >= 15000)
		{
			m_Stage = 2;
			m_SwapAt = now;
			m_LastProbe = 0;
			for (int k = 0; k < m_Objs.Count(); k++)
			{
				int fl = ECE_SETUP | ECE_CREATEPHYSICS | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR;
				if (k != 5)
				{
					fl = fl | ECE_UPDATEPATHGRAPH;
				}
				if (k == 7)
				{
					// control: only delete
					m_SwapDelete.Set(k, 0);
					continue;
				}
				Object nw = g_Game.CreateObjectEx("Land_Container_1Bo", m_From.Get(k), fl);
				m_SwapNew.Set(k, nw);
				m_SwapDelete.Set(k, delays.Get(k));
			}
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			DZOMS_Log.Info("navswap: networked copies created");
		}
		if (m_Stage == 2)
		{
			int rel = now - m_SwapAt;
			for (int d = 0; d < m_Objs.Count(); d++)
			{
				if (m_Objs.Get(d) && m_SwapDelete.Get(d) >= 0 && rel >= m_SwapDelete.Get(d))
				{
					g_Game.ObjectDelete(m_Objs.Get(d));
					m_Objs.Set(d, null);
				}
			}
			if (now - m_LastProbe >= 6000)
			{
				m_LastProbe = now;
				for (int q = 0; q < m_From.Count(); q++)
				{
					DZOMS_Log.Info("navswap t=" + (rel / 1000).ToString() + " site " + q.ToString() + " del=" + m_SwapDelete.Get(q).ToString() + " oldAlive=" + (m_Objs.Get(q) != null).ToString() + " : " + Blocked(m_From.Get(q)));
				}
			}
			if (rel >= 150000)
			{
				g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(SwapTick);
				DZOMS_T.Done();
			}
		}
	}

	// ---- does the navmesh follow a runtime change of a vanilla building at all (doors)?
	ref array<Building> m_Houses = new array<Building>();
	ref array<vector> m_HouseA = new array<vector>();
	ref array<vector> m_HouseB = new array<vector>();
	ref array<string> m_Before = new array<string>();

	static void StartDoor()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_StartMs = g_Game.GetTime();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.DoorTick, 500, true);
	}

	void DoorTick()
	{
		int now = g_Game.GetTime();
		int t = now - m_StartMs;
		if (m_Stage == 0 && t >= 15000)
		{
			m_Stage = 1;
			array<float> xs = {6600, 10300, 8300, 12000, 4600};
			array<float> zs = {2500, 2300, 6000, 8800, 10400};
			for (int i = 0; i < xs.Count(); i++)
			{
				array<Object> found = new array<Object>();
				array<CargoBase> prox = new array<CargoBase>();
				g_Game.GetObjectsAtPosition3D(OnGround(xs.Get(i), zs.Get(i)), 250, found, prox);
				for (int j = 0; j < found.Count() && m_Houses.Count() < 12; j++)
				{
					Building b = Building.Cast(found.Get(j));
					if (!b || b.GetDoorCount() < 1)
					{
						continue;
					}
					vector c = b.GetPosition();
					vector mm[2];
					float r = b.ClippingInfo(mm);
					if (r < 5 || r > 12)
					{
						continue;
					}
					vector a = c - Vector(r + 1.5, 0, 0);
					a[1] = g_Game.SurfaceY(a[0], a[2]);
					vector d = c + Vector(r + 1.5, 0, 0);
					d[1] = g_Game.SurfaceY(d[0], d[2]);
					m_Houses.Insert(b);
					m_HouseA.Insert(a);
					m_HouseB.Insert(d);
					m_Before.Insert(Probe(a, d));
				}
			}
			for (int h = 0; h < m_Houses.Count(); h++)
			{
				Building bb = m_Houses.Get(h);
				for (int dr = 0; dr < bb.GetDoorCount(); dr++)
				{
					bb.OpenDoor(dr);
				}
				g_Game.GetWorld().UpdatePathgraphEntityState(bb);
			}
			m_SwapMs = now;
			DZOMS_Log.Info("navdoor: " + m_Houses.Count().ToString() + " houses, all doors opened and UpdatePathgraphEntityState called");
		}
		if (m_Stage == 1 && now - m_SwapMs >= 4000)
		{
			m_Stage = 2;
			for (int k = 0; k < m_Houses.Count(); k++)
			{
				DZOMS_Log.Info("navdoor " + m_Houses.Get(k).GetType() + " doors=" + m_Houses.Get(k).GetDoorCount().ToString() + " BEFORE " + m_Before.Get(k));
				DZOMS_Log.Info("navdoor " + m_Houses.Get(k).GetType() + " AFTER  " + Probe(m_HouseA.Get(k), m_HouseB.Get(k)));
			}
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(DoorTick);
			DZOMS_T.Done();
		}
	}

	// ---- objects spawned at boot, before any player: does the navmesh take them in when a player arrives?
	int m_ArrivedMs;
	int m_Step;
	Man m_Man;

	static void StartBoot()
	{
		s_Run = new DZOMS_NavProbe();
		s_Run.m_StartMs = g_Game.GetTime();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.BootTick, 500, true);
	}

	void BootTick()
	{
		int now = g_Game.GetTime();
		int t = now - m_StartMs;
		array<string> types = {"Land_Container_1Bo", "StaticObj_Wall_IndCnch_10"};
		array<int> variants = {2, 8, 2 + 4, 4 + 8, 1 + 2, 1 + 8, 1 + 2 + 4, 1 + 4 + 8};
		if (m_Stage == 0 && t >= 8000)
		{
			m_Stage = 1;
			for (int i = 0; i < 8; i++)
			{
				for (int j = 0; j < 8; j++)
				{
					vector c = OnGround(4200 + 60 * i, 10000 + 60 * j);
					if (OpenSite(c))
					{
						m_Sites.Insert(c);
					}
				}
			}
			int site = 0;
			for (int ty = 0; ty < types.Count(); ty++)
			{
				for (int v = 0; v < variants.Count(); v++)
				{
					if (site >= m_Sites.Count())
					{
						break;
					}
					vector pos = m_Sites.Get(site);
					site++;
					Object o = SpawnVariant(variants.Get(v), types.Get(ty), pos);
					if (o)
					{
						m_Objs.Insert(o);
						m_Names.Insert(types.Get(ty) + " " + VariantName(variants.Get(v)));
						m_From.Insert(pos);
					}
				}
			}
			g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			DZOMS_Log.Info("navboot: " + m_Objs.Count().ToString() + " objects spawned at boot, processed, " + m_Sites.Count().ToString() + " open sites");
		}
		if (m_Stage == 1)
		{
			array<Man> men = new array<Man>();
			g_Game.GetPlayers(men);
			if (men.Count() == 0)
			{
				return;
			}
			m_Man = men.Get(0);
			m_ArrivedMs = now;
			m_Stage = 2;
			m_Center = OnGround(4200 + 60 * 3.5, 10000 + 60 * 3.5);
			DZOMS_Log.Info("navboot: a player connected, teleporting to the objects " + m_Center.ToString());
			m_Man.SetPosition(m_Center);
		}
		if (m_Stage == 2)
		{
			int rel = (now - m_ArrivedMs) / 1000;
			if (now - m_LastProbe >= 4000)
			{
				m_LastProbe = now;
				int blocked = 0;
				string line = "";
				for (int k = 0; k < m_Objs.Count(); k++)
				{
					string b = Blocked(m_From.Get(k));
					if (b.Contains("BLOCKED"))
					{
						blocked++;
					}
					int kindOf = k % 6;
					line = line + " " + kindOf.ToString() + b.Substring(0, 2);
				}
				DZOMS_Log.Info("navboot t=" + rel.ToString() + " blocked=" + blocked.ToString() + "/" + m_Objs.Count().ToString() + line);
			}
			if (m_Step == 0 && rel >= 12)
			{
				m_Step = 1;
				DZOMS_Log.Info("navboot: remedies by object index mod 6: 0 none, 1 SetAffect(false,true)+region, 2 SetAffect(true,false)+region, 3 EntityState, 4 SetPosition+Update+region, 5 recreate");
				for (int q = 0; q < m_Objs.Count(); q++)
				{
					Object target = m_Objs.Get(q);
					int kind = q % 6;
					if (kind == 1)
					{
						target.SetAffectPathgraph(false, true);
						g_Game.UpdatePathgraphRegionByObject(target);
					}
					if (kind == 2)
					{
						target.SetAffectPathgraph(true, false);
						g_Game.UpdatePathgraphRegionByObject(target);
					}
					if (kind == 3)
					{
						g_Game.GetWorld().UpdatePathgraphEntityState(target);
					}
					if (kind == 4)
					{
						vector np = target.GetPosition();
						target.SetPosition(np + "0 0.01 0");
						target.SetPosition(np);
						target.Update();
						g_Game.UpdatePathgraphRegionByObject(target);
					}
					if (kind == 5)
					{
						string type = target.GetType();
						vector rp = target.GetPosition();
						g_Game.ObjectDelete(target);
						Object fresh = g_Game.CreateObjectEx(type, rp, ECE_SETUP | ECE_CREATEPHYSICS | ECE_UPDATEPATHGRAPH | ECE_NOLIFETIME | ECE_NOPERSISTENCY_WORLD | ECE_NOPERSISTENCY_CHAR);
						m_Objs.Set(q, fresh);
					}
				}
				g_Game.GetWorld().ProcessMarkedObjectsForPathgraphUpdate();
			}
			if (rel >= 40)
			{
				g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(BootTick);
				DZOMS_T.Done();
			}
		}
	}
}
