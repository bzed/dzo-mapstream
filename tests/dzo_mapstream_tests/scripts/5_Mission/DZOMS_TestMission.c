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
		DZOMS_Test_MapFiles.Run();
		DZOMS_Test_Rules.Run();
		DZOMS_Test_Probe.Run();
		DZOMS_Test_BatchBuilder.Run();
		DZOMS_Test_ServerWorld.Run();
		DZOMS_Test_Requests.Run();
		DZOMS_Test_ClientDownload.Run();
		DZOMS_Test_ClientSpawn.Run();
		DZOMS_Test_Proximity.Run();
	}
}

modded class MissionServer
{
	override void OnInit()
	{
		string scenario = DZOMS_T.Scenario();
		DZOMS_Test_Scenarios.Prepare(scenario);
		super.OnInit();
		DZOMS_Test_Scenarios.Check(scenario);
		if (scenario == "unit")
		{
			DZOMS_TestSuites.RunAll();
		}
		if (scenario == "navprobe")
		{
			DZOMS_NavProbe.StartTimeline();
			return;
		}
		if (scenario == "navboot")
		{
			DZOMS_NavProbe.StartBoot();
			return;
		}
		if (scenario == "navclient")
		{
			DZOMS_NavProbe.StartClientSpawn();
			return;
		}
		if (scenario == "navdoor")
		{
			DZOMS_NavProbe.StartDoor();
			return;
		}
		if (scenario == "navspawn")
		{
			DZOMS_NavProbe.StartSpawn();
			return;
		}
		if (scenario == "navcontrol")
		{
			DZOMS_NavProbe.StartControl();
			return;
		}
		if (scenario == "navscan")
		{
			DZOMS_NavProbe.StartScan();
			return;
		}
		DZOMS_T.Done();
	}
}
