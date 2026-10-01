class DZOMS_Test_Smoke
{
	static void Run()
	{
		DZOMS_T.Suite("smoke");
		DZOMS_T.EqInt(DZOMS_Rpc.REQUEST_MANIFEST, 1146768641, "REQUEST_MANIFEST");
		DZOMS_T.EqInt(DZOMS_Rpc.BATCH, 1146768644, "BATCH");
		DZOMS_T.EqInt(DZOMS_Rpc.LAST - DZOMS_Rpc.FIRST, 3, "rpc id span");
		DZOMS_T.Check(g_Game.IsDedicatedServer(), "runs on the dedicated server");
	}
}
