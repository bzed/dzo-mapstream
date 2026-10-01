class DZOMS_Test_Glob
{
	static void M(string p, string s, bool want)
	{
		string got = "no";
		if (DZOMS_Glob.Match(p, s))
		{
			got = "yes";
		}
		string w = "no";
		if (want)
		{
			w = "yes";
		}
		DZOMS_T.Eq(got, w, "glob '" + p + "' vs '" + s + "'");
	}

	static void Run()
	{
		DZOMS_T.Suite("glob");
		M("land_lighthouse", "land_lighthouse", true);
		M("land_lighthouse", "land_lighthouse2", false);
		M("land_lighthouse", "land_lighthous", false);
		M("*stair*", "land_staircase", true);
		M("*stair*", "stair", true);
		M("*stair*", "land_stai", false);
		M("land_?ouse", "land_house", true);
		M("land_?ouse", "land_ouse", false);
		M("*", "", true);
		M("", "", true);
		M("", "a", false);
		M("a", "", false);
		M("a*b*c", "axxbyyc", true);
		M("a*b*c", "axxbyy", false);
		M("*a", "ba", true);
		M("*a", "ab", false);
		M("**x", "x", true);
		M("a*", "a", true);
		M("*ab", "aab", true);
		M("a*ab", "aaab", true);
		M("dz/structures_bliss/*", "dz/structures_bliss/residential/x.p3d", true);
		M("dz/structures_bliss/*", "dz/structures/residential/x.p3d", false);

		DZOMS_T.Eq(DZOMS_TypeName.Of("Land_House_1W10"), "land_house_1w10", "Of class name");
		DZOMS_T.Eq(DZOMS_TypeName.Of("dz\\structures\\ruins\\Ancient_Ruins_StairCellar.p3d"), "ancient_ruins_staircellar", "Of model path");
		DZOMS_T.Eq(DZOMS_TypeName.Of("plain.p3d"), "plain", "Of plain model");
		DZOMS_T.Eq(DZOMS_TypeName.Normalize("\\DZ\\Structures\\X.p3d"), "dz/structures/x.p3d", "Normalize");
		DZOMS_T.Check(DZOMS_TypeName.IsModel("a\\B.P3D"), "IsModel upper case");
		DZOMS_T.Check(!DZOMS_TypeName.IsModel("Land_House_1W10"), "IsModel class");
		DZOMS_T.Eq(DZOMS_TypeName.ModelPath("DZOMS_No_Such_Class"), "", "ModelPath unknown class");
		string mp = DZOMS_TypeName.ModelPath("Land_House_1W10");
		DZOMS_T.Check(mp.IndexOf("dz/") == 0 && mp.Contains(".p3d"), "ModelPath of a vanilla house: '" + mp + "'");
		DZOMS_T.Eq(DZOMS_TypeName.ModelPath("A\\B\\C.p3d"), "a/b/c.p3d", "ModelPath of a model type");
	}
}
