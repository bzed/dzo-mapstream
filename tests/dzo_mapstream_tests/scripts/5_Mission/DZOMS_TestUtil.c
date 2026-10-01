class DZOMS_TestUtil
{
	// empty air in the north-west of Chernarus; every suite uses its own offset
	static const vector TEST_ORIGIN = "1000 500 14000";

	static Object SpawnLocal(string type, vector pos)
	{
		return g_Game.CreateObjectEx(type, pos, ECE_LOCAL | ECE_CREATEPHYSICS | ECE_NOLIFETIME);
	}

	static void Delete(Object obj)
	{
		if (obj)
		{
			g_Game.ObjectDelete(obj);
		}
	}
}
