// PROBE (throwaway, branch probe/climb-log): logs every jump/climb press on the client and on the
// server, to compare what both sides find. Not part of any release. Print() cuts at 256
// characters, so every line stays short.
class DZOMS_ClimbProbe
{
	static string Origin(Object o)
	{
		DayZGame game = GetDayZGame();
		if (g_Game.IsServer())
		{
			DZOMS_ServerWorld sw = game.DZOMS_GetServerWorld();
			if (sw && sw.GetSpawned().Find(o) >= 0)
			{
				return "mapstream";
			}
			return "other";
		}
		DZOMS_ClientWorld cw = game.DZOMS_GetClientWorld();
		if (cw)
		{
			for (int i = 0; i < cw.GetBatchCount(); i++)
			{
				DZOMS_ClientBatch cb = cw.GetClientBatch(i);
				if (cb.objects && cb.objects.Find(o) >= 0)
				{
					return "mapstream";
				}
				if (cb.proxies && cb.proxies.Find(o) >= 0)
				{
					return "mapstream-proxy";
				}
			}
		}
		return "other";
	}

	static string Describe(IEntity parent)
	{
		if (!parent)
		{
			return "none";
		}
		Object o = Object.Cast(parent);
		if (!o)
		{
			return "non-object";
		}
		int low, high;
		o.GetNetworkID(low, high);
		string pos = o.GetPosition().ToString();
		return o.GetType() + " shape=" + o.GetShapeName() + " net=" + low.ToString() + "/" + high.ToString() + " " + Origin(o) + " pos=" + pos;
	}

	// objects the world reports within 2 m of a point 1.2 m in front of the player, and what a short
	// forward ray hits at knee and waist height
	static void Around(Human p, string head)
	{
		vector pos = p.GetPosition();
		vector dir = p.GetDirection();
		array<Object> objs = new array<Object>();
		array<CargoBase> prox = new array<CargoBase>();
		g_Game.GetObjectsAtPosition3D(pos + dir * 1.2, 2.0, objs, prox);
		DZOMS_Log.Info(head + "   near " + objs.Count().ToString() + " objects within 2 m of " + (pos + dir * 1.2).ToString());
		int shown = 0;
		for (int i = 0; i < objs.Count() && shown < 4; i++)
		{
			Object o = objs.Get(i);
			if (!o || o == p)
			{
				continue;
			}
			int low, high;
			o.GetNetworkID(low, high);
			DZOMS_Log.Info(head + "   near " + o.GetType() + " net=" + low.ToString() + " " + Origin(o) + " pos=" + o.GetPosition().ToString() + " ori=" + o.GetOrientation().ToString());
			shown++;
		}
		array<float> heights = {0.5, 1.0};
		for (int h = 0; h < heights.Count(); h++)
		{
			vector from = pos + Vector(0, heights.Get(h), 0);
			vector contact;
			vector cdir;
			int comp;
			set<Object> hits = new set<Object>();
			bool hit = DayZPhysics.RaycastRV(from, from + dir * 2.0, contact, cdir, comp, hits, null, p, false, false, ObjIntersectGeom, 0.0);
			string what = "";
			if (hits.Count() > 0 && hits.Get(0))
			{
				int hl, hh;
				hits.Get(0).GetNetworkID(hl, hh);
				what = hits.Get(0).GetType() + " net=" + hl.ToString() + " " + Origin(hits.Get(0));
			}
			DZOMS_Log.Info(head + "   ray y+" + heights.Get(h).ToString() + " hit=" + hit.ToString() + " " + what);
		}
	}

	static string Side()
	{
		if (g_Game.IsServer())
		{
			return "SERVER";
		}
		return "CLIENT";
	}
}

modded class DayZPlayerImplement
{
	override bool ProcessJumpOrClimb(float pDt, int pCurrentCommandID)
	{
		HumanInputController hic = GetInputController();
		if (hic && hic.IsJumpClimb())
		{
			SHumanCommandClimbResult res = new SHumanCommandClimbResult();
			HumanCommandClimb.DoPerformClimbTest(this, res, 0);
			string head = "climb-probe " + DZOMS_ClimbProbe.Side() + " t=" + g_Game.GetTime().ToString();
			DZOMS_Log.Info(head + " cmd=" + pCurrentCommandID.ToString() + " pos=" + GetPosition().ToString() + " climb=" + res.m_bIsClimb.ToString() + " over=" + res.m_bIsClimbOver.ToString() + " h=" + res.m_fClimbHeight.ToString());
			DZOMS_Log.Info(head + "   grab " + DZOMS_ClimbProbe.Describe(res.m_GrabPointParent));
			DZOMS_Log.Info(head + "   stand " + DZOMS_ClimbProbe.Describe(res.m_ClimbStandPointParent));
			DZOMS_Log.Info(head + "   over " + DZOMS_ClimbProbe.Describe(res.m_ClimbOverStandPointParent));
			DZOMS_ClimbProbe.Around(this, head);
			bool done = super.ProcessJumpOrClimb(pDt, pCurrentCommandID);
			bool climbing = GetCommand_Climb() != null;
			DZOMS_Log.Info(head + "   result " + done.ToString() + " climbCmd=" + climbing.ToString());
			return done;
		}
		return super.ProcessJumpOrClimb(pDt, pCurrentCommandID);
	}
}
