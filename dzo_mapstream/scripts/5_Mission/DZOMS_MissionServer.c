// The players proximity networking looks at (DESIGN 13.3)
class DZOMS_PlayerStates
{
	static array<ref DZOMS_PlayerState> Collect()
	{
		array<ref DZOMS_PlayerState> states = new array<ref DZOMS_PlayerState>();
		array<Man> men = new array<Man>();
		g_Game.GetPlayers(men);
		for (int i = 0; i < men.Count(); i++)
		{
			PlayerBase player = PlayerBase.Cast(men.Get(i));
			if (!player)
			{
				continue;
			}
			DZOMS_PlayerState state = new DZOMS_PlayerState();
			state.id = i;
			state.pos = player.GetPosition();
			state.alive = player.IsAlive();
			states.Insert(state);
		}
		return states;
	}
}

modded class MissionServer
{
	protected float m_DZOMS_SweepTimer;

	override void OnInit()
	{
		super.OnInit();

		DZOMS_Config config = DZOMS_Config.Load(DZOMS_Config.PROFILE_DIR);
		DZOMS_ObjectProbe probe = new DZOMS_ObjectProbeWorld();
		DZOMS_ServerWorld world = new DZOMS_ServerWorld(config, DZOMS_Config.MAPS_DIR, probe);
		// registered before Load(): requests are only answered once the world exists, but tests look it up early
		GetDayZGame().DZOMS_SetServerWorld(world);
		world.Load();
		world.StartProximity();
		DZOMS_Log.Info("ready: digest " + world.GetDigest());
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);

		DZOMS_ServerWorld proximityWorld = GetDayZGame().DZOMS_GetServerWorld();
		if (proximityWorld && proximityWorld.GetProximity())
		{
			int now = g_Game.GetTime();
			DZOMS_Proximity proximity = proximityWorld.GetProximity();
			if (proximity.NeedsUpdate(now))
			{
				proximity.Update(DZOMS_PlayerStates.Collect(), now);
			}
			proximity.ProcessQueue(now);
		}

		m_DZOMS_SweepTimer += timeslice;
		if (m_DZOMS_SweepTimer >= 60)
		{
			m_DZOMS_SweepTimer = 0;
			DZOMS_ServerWorld world = GetDayZGame().DZOMS_GetServerWorld();
			if (world)
			{
				world.SweepRateLimits(g_Game.GetTime());
			}
		}
	}

	// not InvokeOnDisconnect: that one is skipped when the player entity is already gone and has no uid
	override void PlayerDisconnected(PlayerBase player, PlayerIdentity identity, string uid)
	{
		DZOMS_ServerWorld world = GetDayZGame().DZOMS_GetServerWorld();
		if (world)
		{
			world.OnPlayerDisconnected(uid);
		}
		super.PlayerDisconnected(player, identity, uid);
	}
}
