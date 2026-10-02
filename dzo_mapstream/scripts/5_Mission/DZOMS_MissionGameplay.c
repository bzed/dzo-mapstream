modded class MissionGameplay
{
	override void OnInit()
	{
		super.OnInit();

		if (g_Game.IsDedicatedServer())
		{
			return;
		}
		DZOMS_ClientWorld world = new DZOMS_ClientWorld(DZOMS_ClientWorld.CachePathForHost());
		GetDayZGame().DZOMS_SetClientWorld(world);
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate(timeslice);

		DZOMS_ClientWorld world = GetDayZGame().DZOMS_GetClientWorld();
		if (!world)
		{
			return;
		}
		bool inGame = g_Game.GetGameState() == DayZGameState.IN_GAME;
		world.Update(g_Game.GetCurrentCameraPosition(), g_Game.GetTime(), inGame);
	}

	override void OnMissionFinish()
	{
		DZOMS_ClientWorld world = GetDayZGame().DZOMS_GetClientWorld();
		if (world)
		{
			world.Destroy();
			GetDayZGame().DZOMS_SetClientWorld(null);
		}
		super.OnMissionFinish();
	}
}
