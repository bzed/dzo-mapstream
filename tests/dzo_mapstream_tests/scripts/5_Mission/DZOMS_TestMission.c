class DZOMS_TestSuites
{
	static void RunAll()
	{
		DZOMS_Test_Smoke.Run();
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
