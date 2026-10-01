// Minimal test framework: counts checks, prints failures for the runner script.
class DZOMS_T
{
	static const string DIR = "$profile:dzo-mapstream-tests";

	static int s_Pass;
	static int s_Fail;
	static string s_Suite;

	static void Suite(string name)
	{
		s_Suite = name;
		Log("suite " + name);
	}

	static void Log(string msg)
	{
		Print("[dzo-mapstream-test] " + msg);
	}

	static void Check(bool ok, string what)
	{
		if (ok)
		{
			s_Pass++;
			return;
		}
		s_Fail++;
		Log("FAIL " + s_Suite + ": " + what);
	}

	static void Eq(string got, string want, string what)
	{
		if (got == want)
		{
			s_Pass++;
			return;
		}
		s_Fail++;
		Log("FAIL " + s_Suite + ": " + what + " got '" + got + "' want '" + want + "'");
	}

	static void EqInt(int got, int want, string what)
	{
		if (got == want)
		{
			s_Pass++;
			return;
		}
		s_Fail++;
		Log("FAIL " + s_Suite + ": " + what + " got '" + got.ToString() + "' want '" + want.ToString() + "'");
	}

	static void EqFloat(float got, float want, string what)
	{
		if (Math.AbsFloat(got - want) <= 0.001)
		{
			s_Pass++;
			return;
		}
		s_Fail++;
		Log("FAIL " + s_Suite + ": " + what + " got '" + got.ToString() + "' want '" + want.ToString() + "'");
	}

	static void Done()
	{
		Log("DONE pass=" + s_Pass.ToString() + " fail=" + s_Fail.ToString());
	}

	// first line of $profile:dzo-mapstream-tests/scenario.txt, "" if missing
	static string Scenario()
	{
		string line = "";
		FileHandle fh = OpenFile(DIR + "/scenario.txt", FileMode.READ);
		if (fh == 0)
		{
			return "";
		}
		FGets(fh, line);
		CloseFile(fh);
		line.TrimInPlace();
		return line;
	}
}
