// Scenario "clientview": a real client joins, the server walks the player to a spot in front of each
// object of the loaded map and logs "view stop N"; tools/run-client-view.sh takes a screenshot per stop.
class DZOMS_ClientView
{
	static const int STOP_MS = 30000;
	static const float STANDOFF = 35;

	static ref DZOMS_ClientView s_Run;
	int m_StartMs;
	int m_StopMs;
	int m_Stop = -1;

	static void Start()
	{
		s_Run = new DZOMS_ClientView();
		s_Run.m_StartMs = g_Game.GetTime();
		g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(s_Run.Tick, 1000, true);
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
		if (m_Stop >= 0 && now - m_StopMs < STOP_MS)
		{
			return;
		}
		m_Stop++;
		m_StopMs = now;
		array<ref DZOMS_ObjectEntry> objs = w.GetObjects();
		if (m_Stop >= objs.Count())
		{
			g_Game.GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(Tick);
			DZOMS_T.Log("view done");
			DZOMS_T.Done();
			return;
		}
		DZOMS_ObjectEntry e = objs.Get(m_Stop);
		vector from = e.pos + Vector(STANDOFF, 0, 0);
		from[1] = g_Game.SurfaceY(from[0], from[2]);
		player.SetAllowDamage(false);
		player.SetPosition(from);
		vector look = e.pos - from;
		look[1] = 0;
		player.SetOrientation(look.VectorToAngles());
		DZOMS_T.Log("view stop " + m_Stop.ToString() + " " + e.type + " at " + e.pos.ToString() + " scale " + e.scale.ToString() + " from " + from.ToString() + " flags " + e.flags.ToString());
	}
}
