// Scenario "rockview": a real client joins a Livonia server holding one .dze; the server puts the player
// next to a dense cluster of rocks (A), far away (B, the rocks demote), back (A again, they promote again).
// The client probe follows six rocks and logs every change of what stands around each of them.
// tools/run-rock-view.sh takes the screenshots.
class DZOMS_RockView
{
	static const int PHASE_MS = 40000;
	static const float STANDOFF = 25;

	static ref DZOMS_RockView s_Run;
	int m_PhaseMs;
	int m_Phase = -1;
	vector m_A;
	vector m_Look;

	static void Start()
	{
		s_Run = new DZOMS_RockView();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.Tick, 1000, true);
	}

	// the rock with the most rocks within 40 m
	protected bool FindCluster(array<ref DZOMS_ObjectEntry> objs)
	{
		int best = 0;
		int bestAt = -1;
		for (int i = 0; i < objs.Count(); i++)
		{
			if (!DZOMS_RockProbe.IsRock(objs.Get(i).type))
			{
				continue;
			}
			int n = 0;
			for (int j = 0; j < objs.Count(); j++)
			{
				if (DZOMS_RockProbe.IsRock(objs.Get(j).type) && vector.Distance(objs.Get(i).pos, objs.Get(j).pos) < 40)
				{
					n++;
				}
			}
			if (n > best)
			{
				best = n;
				bestAt = i;
			}
		}
		if (bestAt < 0)
		{
			return false;
		}
		m_Look = objs.Get(bestAt).pos;
		m_A = m_Look + Vector(STANDOFF, 0, 0);
		m_A[1] = g_Game.SurfaceY(m_A[0], m_A[2]);
		DZOMS_T.Log("rock cluster of " + best.ToString() + " at " + m_Look.ToString() + ", player at " + m_A.ToString());
		return true;
	}

	void Tick()
	{
		array<Man> men = new array<Man>();
		g_Game.GetPlayers(men);
		DZOMS_ServerWorld w = GetDayZGame().DZOMS_GetServerWorld();
		if (men.Count() == 0 || !w)
		{
			return;
		}
		PlayerBase player = PlayerBase.Cast(men.Get(0));
		if (!player)
		{
			return;
		}
		int now = g_Game.GetTime();
		if (m_Phase >= 0 && now - m_PhaseMs < PHASE_MS)
		{
			return;
		}
		if (m_Phase < 0 && !FindCluster(w.GetObjects()))
		{
			DZOMS_T.Log("FAIL no rocks in the map");
			DZOMS_T.Done();
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(Tick);
			return;
		}
		m_Phase++;
		m_PhaseMs = now;
		if (m_Phase > 2)
		{
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(Tick);
			DZOMS_T.Log("rock done");
			DZOMS_T.Done();
			return;
		}
		vector at = m_A;
		string name = "A";
		if (m_Phase == 1)
		{
			at = m_A + Vector(600, 0, 0);
			at[1] = g_Game.SurfaceY(at[0], at[2]);
			name = "B";
		}
		player.SetAllowDamage(false);
		player.SetPosition(at);
		vector look = m_Look - at;
		look[1] = 0;
		player.SetOrientation(look.VectorToAngles());
		DZOMS_T.Log("rock phase " + m_Phase.ToString() + " " + name + " at " + at.ToString());
	}
}

// client side: follows six rocks and logs a line whenever what stands within 6 m of one of them changes
class DZOMS_RockProbe
{
	static ref DZOMS_RockProbe s_Run;
	ref array<vector> m_Track = new array<vector>();
	ref array<string> m_Last = new array<string>();
	int m_Seen;
	int m_SeenLogged;

	static bool IsRock(string type)
	{
		string t = type;
		t.ToLower();
		return t.Contains("rock");
	}

	// the map's own rocks are called Static_rock_*; the rocks of the .dze are bldr_rock_* (BuilderItems)
	static bool IsMapRock(string type)
	{
		string t = type;
		t.ToLower();
		return t.IndexOf("bldr_rock") == 0;
	}

	static void Start()
	{
		s_Run = new DZOMS_RockProbe();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.Tick, 200, true);
	}

	protected void Pick(vector cam)
	{
		array<Object> objs = new array<Object>();
		array<CargoBase> proxies = new array<CargoBase>();
		g_Game.GetObjectsAtPosition3D(cam, 60, objs, proxies);
		array<Object> rocks = new array<Object>();
		for (int i = 0; i < objs.Count(); i++)
		{
			if (IsRock(objs.Get(i).GetType()))
			{
				m_Seen++;
			}
			if (IsMapRock(objs.Get(i).GetType()))
			{
				rocks.Insert(objs.Get(i));
			}
		}
		if (m_Seen > 0 && m_Seen != m_SeenLogged)
		{
			m_SeenLogged = m_Seen;
			DZOMS_T.Log("rockprobe sees " + m_Seen.ToString() + " rocks of any kind, " + rocks.Count().ToString() + " bldr_rock");
		}
		m_Seen = 0;
		if (rocks.Count() < 6)
		{
			return;
		}
		for (int k = 0; k < 6; k++)
		{
			int at = 0;
			float d = 100000;
			for (int j = 0; j < rocks.Count(); j++)
			{
				float dj = vector.Distance(rocks.Get(j).GetPosition(), cam);
				if (dj < d)
				{
					d = dj;
					at = j;
				}
			}
			m_Track.Insert(rocks.Get(at).GetPosition());
			m_Last.Insert("");
			DZOMS_T.Log("rockprobe track " + k.ToString() + " " + rocks.Get(at).GetType() + " at " + rocks.Get(at).GetPosition().ToString() + " scale " + rocks.Get(at).GetScale().ToString());
			rocks.RemoveOrdered(at);
		}
	}

	void Tick()
	{
		if (g_Game.GetGameState() != DayZGameState.IN_GAME)
		{
			return;
		}
		vector cam = g_Game.GetCurrentCameraPosition();
		if (m_Track.Count() == 0)
		{
			Pick(cam);
			return;
		}
		for (int i = 0; i < m_Track.Count(); i++)
		{
			array<Object> objs = new array<Object>();
			array<CargoBase> proxies = new array<CargoBase>();
			g_Game.GetObjectsAtPosition3D(m_Track.Get(i), 6, objs, proxies);
			array<string> parts = new array<string>();
			for (int j = 0; j < objs.Count(); j++)
			{
				Object o = objs.Get(j);
				if (!IsRock(o.GetType()))
				{
					continue;
				}
				string tn = o.GetType();
				tn.Replace("_rock_bright_", ":");
				int low;
				int high;
				o.GetNetworkID(low, high);
				string kind = "local";
				if (low != 0 || high != 0)
				{
					kind = "net" + low.ToString();
				}
				parts.Insert(tn + " " + kind + " d=" + (Math.Round(vector.Distance(o.GetPosition(), m_Track.Get(i)) * 100) / 100).ToString() + " s=" + o.GetScale().ToString() + " y=" + o.GetOrientation()[0].ToString());
			}
			parts.Sort();
			string sig = "";
			for (int p = 0; p < parts.Count(); p++)
			{
				sig = sig + "[" + parts.Get(p) + "] ";
			}
			if (sig != m_Last.Get(i))
			{
				m_Last.Set(i, sig);
				DZOMS_T.Log("rockprobe " + i.ToString() + " camdist " + Math.Round(vector.Distance(cam, m_Track.Get(i))).ToString() + " " + sig);
			}
		}
	}
}

modded class MissionGameplay
{
	override void OnInit()
	{
		super.OnInit();
		DZOMS_RockProbe.Start();
	}
}
