// Type name of an object (DESIGN.md 3): the class name, or for a model path
// the file name without directory and ".p3d", always lower case.
class DZOMS_TypeName
{
	static bool IsModel(string type)
	{
		string t = type;
		t.ToLower();
		return t.Contains(".p3d");
	}

	// lower case, '\' -> '/', no leading '/'
	static string Normalize(string s)
	{
		string t = s;
		t.ToLower();
		t.Replace("\\", "/");
		while (t.Length() > 0 && t.Get(0) == "/")
		{
			t = t.Substring(1, t.Length() - 1);
		}
		return t;
	}

	static string Of(string type)
	{
		string t = Normalize(type);
		if (!IsModel(type))
		{
			return t;
		}
		int slash = t.LastIndexOf("/");
		if (slash >= 0)
		{
			t = t.Substring(slash + 1, t.Length() - slash - 1);
		}
		int ext = t.LastIndexOf(".p3d");
		if (ext >= 0)
		{
			t = t.Substring(0, ext);
		}
		return t;
	}

	// normalized model path: the type itself for .p3d types, else CfgVehicles <type> model; "" if absent
	static string ModelPath(string type)
	{
		if (IsModel(type))
		{
			return Normalize(type);
		}
		string model;
		if (!g_Game.ConfigGetText("CfgVehicles " + type + " model", model))
		{
			return "";
		}
		return Normalize(model);
	}
}
