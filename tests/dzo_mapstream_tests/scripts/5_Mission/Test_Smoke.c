class DZOMS_Test_Smoke
{
	static void Run()
	{
		DZOMS_T.Suite("smoke");
		DZOMS_T.EqInt(DZOMS_Rpc.REQUEST_MANIFEST, 1146768641, "REQUEST_MANIFEST");
		DZOMS_T.EqInt(DZOMS_Rpc.BATCH, 1146768644, "BATCH");
		DZOMS_T.EqInt(DZOMS_Rpc.LAST - DZOMS_Rpc.FIRST, 5, "rpc id span");
		DZOMS_T.Check(g_Game.IsDedicatedServer(), "runs on the dedicated server");

		// client classes are compiled on the server too (every boot proves they compile)
		DZOMS_ClientWorld client = new DZOMS_ClientWorld("");
		DZOMS_T.Check(client != null, "client world can be constructed with an empty path");
		DZOMS_T.Check(!client.IsComplete(), "new client world is not complete");
	}
}
