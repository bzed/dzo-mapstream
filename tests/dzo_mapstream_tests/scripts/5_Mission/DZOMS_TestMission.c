class DZOMS_TestSuites
{
	static void RunAll()
	{
		DZOMS_Test_Smoke.Run();
		DZOMS_Test_Sha256.Run();
		DZOMS_Test_Hasher.Run();
		DZOMS_Test_Cache.Run();
		DZOMS_Test_Glob.Run();
		DZOMS_Test_Config.Run();
		DZOMS_Test_Rules.Run();
		DZOMS_Test_Probe.Run();
		DZOMS_Test_BatchBuilder.Run();
		DZOMS_Test_ServerWorld.Run();
	}
}

modded class MissionServer
{
	override void OnInit()
	{
		super.OnInit();
		DZOMS_TestSuites.RunAll();
		DZOMS_T.Done();
	}
}
