class DZOMS_Log
{
	static void Info(string msg)
	{
		Print("[dzo-mapstream] " + msg);
	}

	static void Warn(string msg)
	{
		Print("[dzo-mapstream] WARNING: " + msg);
	}

	static void Err(string msg)
	{
		Print("[dzo-mapstream] ERROR: " + msg);
	}
}
