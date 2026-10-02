modded class MissionServer
{
	protected float m_DZOMS_SweepTimer;

	override void OnInit()
	{
		super.OnInit();

		DZOMS_Config config = DZOMS_Config.Load(DZOMS_Config.PROFILE_DIR);
		DZOMS_ObjectProbe probe = new DZOMS_ObjectProbeWorld();
		DZOMS_ServerWorld world = new DZOMS_ServerWorld(config, DZOMS_Config.PROFILE_DIR + "/maps", probe);
		// registered before Load(): requests are only answered once the world exists, but tests look it up early
		GetDayZGame().DZOMS_SetServerWorld(world);
		world.Load();
		DZOMS_Log.Info("ready: digest " + world.GetDigest());
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);

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
