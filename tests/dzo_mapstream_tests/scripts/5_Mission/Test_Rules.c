class DZOMS_Test_Rules
{
	static DZOMS_NetworkRules Make(array<string> texts)
	{
		array<ref DZOMS_Rule> rules = new array<ref DZOMS_Rule>();
		array<string> errors = new array<string>();
		DZOMS_NetworkRules.Parse(texts, rules, errors);
		array<ref DZOMS_Rule> none = new array<ref DZOMS_Rule>();
		return DZOMS_NetworkRules.Combine(rules, none);
	}

	static void D(DZOMS_NetworkRules nr, string type, int want, string wantRule, string what)
	{
		string rule;
		int got = nr.Decide(type, rule);
		DZOMS_T.EqInt(got, want, what);
		DZOMS_T.Eq(rule, wantRule, what + " (rule)");
	}

	static void L(string name, bool want)
	{
		DZOMS_T.Check(DZOMS_NetworkRules.IsLadderSelection(name) == want, "IsLadderSelection('" + name + "') == " + want.ToString());
	}

	static void Run()
	{
		DZOMS_T.Suite("rules");
		DZOMS_NetworkRules.ClearTokenCache();
		int N = DZOMS_NetworkRules.NETWORKED;
		int LO = DZOMS_NetworkRules.LOCAL;
		int NEED = DZOMS_NetworkRules.NEEDS_OBJECT;

		// parsing
		array<string> texts = {"@doors", "!@ladders", "Land_X", "!*Y*", "@bogus", "", "!", "  ", "dz\\Structures_Bliss\\*", "@fixedItems", "@ACTIONS"};
		array<ref DZOMS_Rule> rules = new array<ref DZOMS_Rule>();
		array<string> errors = new array<string>();
		DZOMS_NetworkRules.Parse(texts, rules, errors);
		DZOMS_T.EqInt(rules.Count(), 7, "parse: 7 rules");
		DZOMS_T.EqInt(errors.Count(), 4, "parse: 4 errors");
		if (rules.Count() == 7)
		{
			DZOMS_T.Check(rules.Get(1).negated && rules.Get(1).token == DZOMS_Rule.TOKEN_LADDERS, "parse: !@ladders");
			DZOMS_T.Eq(rules.Get(3).pattern, "*y*", "parse: pattern lower-cased");
			DZOMS_T.Check(rules.Get(3).negated, "parse: !*Y* negated");
			DZOMS_T.Eq(rules.Get(4).pattern, "dz/structures_bliss/*", "parse: path pattern normalized");
			DZOMS_T.Check(rules.Get(4).matchPath, "parse: path rule flagged");
			DZOMS_T.Eq(rules.Get(4).text, "dz\\Structures_Bliss\\*", "parse: original text kept");
			DZOMS_T.EqInt(rules.Get(6).token, DZOMS_Rule.TOKEN_ACTIONS, "parse: @ACTIONS case-insensitive");
			DZOMS_T.EqInt(rules.Get(5).token, DZOMS_Rule.TOKEN_FIXED_ITEMS, "parse: @fixedItems");
		}

		// last matching rule wins
		array<string> t1 = {"*Container*", "!Land_Container_1Bo"};
		DZOMS_NetworkRules r1 = Make(t1);
		D(r1, "Land_Container_1Bo", LO, "!Land_Container_1Bo", "container 1Bo local");
		D(r1, "LAND_CONTAINER_1BO", LO, "!Land_Container_1Bo", "container 1Bo upper case");
		D(r1, "land_container_2", N, "*Container*", "container 2 networked");
		D(r1, "Land_House", LO, "", "no match is local");

		array<string> t2 = {"!*", "Land_A"};
		DZOMS_NetworkRules r2 = Make(t2);
		D(r2, "Land_A", N, "Land_A", "A networked");
		D(r2, "Land_B", LO, "!*", "B local by !*");

		array<string> t3 = {"*staircellar"};
		D(Make(t3), "dz\\structures\\ruins\\Ancient_Ruins_StairCellar.p3d", N, "*staircellar", "p3d type matches by type name");

		// order of Combine
		array<string> g = {"*Container*"};
		array<string> m = {"!Land_Container_1Bo"};
		array<ref DZOMS_Rule> gr = new array<ref DZOMS_Rule>();
		array<ref DZOMS_Rule> mr = new array<ref DZOMS_Rule>();
		DZOMS_NetworkRules.Parse(g, gr, errors);
		DZOMS_NetworkRules.Parse(m, mr, errors);
		D(DZOMS_NetworkRules.Combine(gr, mr), "Land_Container_1Bo", LO, "!Land_Container_1Bo", "global then map");
		D(DZOMS_NetworkRules.Combine(mr, gr), "Land_Container_1Bo", N, "*Container*", "map then global");

		// rules that need the object
		array<string> t4 = {"@doors", "Land_A"};
		DZOMS_NetworkRules r4 = Make(t4);
		D(r4, "Land_A", N, "Land_A", "later name rule decides without object");
		D(r4, "Land_B", NEED, "", "earlier @ rule needs the object");
		array<string> t5 = {"Land_A", "!@doors"};
		D(Make(t5), "Land_A", NEED, "", "last @ rule needs the object");

		// path rule
		string mp = DZOMS_TypeName.ModelPath("Land_House_1W10");
		mp.Replace("/", "\\");
		array<string> t6 = {mp};
		DZOMS_NetworkRules r6 = Make(t6);
		D(r6, "Land_House_1W10", N, mp, "path rule matches the house");
		D(r6, "Land_Container_1Bo", LO, "", "path rule does not match the container");

		// ladder selection names
		L("ladder1", true);
		L("Ladder12", true);
		L("ladder", false);
		L("ladders", false);
		L("ladder1a", false);
		L("xladder1", false);

		// real objects
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 100";
		Object house = DZOMS_TestUtil.SpawnLocal("Land_House_1W10", at);
		Object apple = DZOMS_TestUtil.SpawnLocal("Apple", at + "20 0 0");
		DZOMS_T.Check(house != null, "house spawned");
		DZOMS_T.Check(DZOMS_NetworkRules.HasDoors(house), "house has doors");
		DZOMS_T.Check(!DZOMS_NetworkRules.HasDoors(apple), "apple has no doors");
		DZOMS_T.Check(!DZOMS_NetworkRules.HasDoors(null), "NULL has no doors");

		array<string> t7 = {"@doors"};
		DZOMS_NetworkRules r7 = Make(t7);
		string rule;
		DZOMS_ObjectProbe probe = new DZOMS_ObjectProbe();
		DZOMS_T.EqInt(r7.DecideWithObject("Land_House_1W10", house, probe, rule), N, "@doors networks the house");
		DZOMS_T.Eq(rule, "@doors", "@doors rule text");
		D(r7, "Land_House_1W10", N, "@doors", "cached after DecideWithObject");
		DZOMS_T.EqInt(r7.DecideWithObject("Apple", apple, probe, rule), LO, "@doors leaves the apple local");
		DZOMS_T.EqInt(r7.DecideWithObject("Land_Nothing", null, probe, rule), LO, "NULL object: @ rule does not match");
		D(r7, "Land_Nothing", NEED, "", "NULL result was not cached");
		array<string> t8 = {"@doors", "!@doors"};
		DZOMS_T.EqInt(Make(t8).DecideWithObject("Land_House_1W10", house, probe, rule), LO, "@doors then !@doors is local");
		DZOMS_TestUtil.Delete(house);
		DZOMS_TestUtil.Delete(apple);

		// ladders: find a vanilla type that has a ladder selection
		string found = "";
		int tried = 0;
		int count = g_Game.ConfigGetChildrenCount("CfgVehicles");
		for (int i = 0; i < count && found == "" && tried < 80; i++)
		{
			string cn;
			g_Game.ConfigGetChildName("CfgVehicles", i, cn);
			string low = cn;
			low.ToLower();
			if (low.IndexOf("land_") != 0)
			{
				continue;
			}
			if (!low.Contains("tower") && !low.Contains("ladder") && !low.Contains("silo") && !low.Contains("tank"))
			{
				continue;
			}
			if (g_Game.ConfigGetInt("CfgVehicles " + cn + " scope") < 1)
			{
				continue;
			}
			Object cand = DZOMS_TestUtil.SpawnLocal(cn, DZOMS_TestUtil.TEST_ORIGIN + "0 0 200");
			if (!cand)
			{
				continue;
			}
			tried++;
			if (DZOMS_NetworkRules.HasLadder(cand))
			{
				found = cn;
				array<string> t9 = {"@ladders"};
				DZOMS_T.EqInt(Make(t9).DecideWithObject(cn, cand, probe, rule), N, "@ladders networks " + cn);
			}
			DZOMS_TestUtil.Delete(cand);
		}
		DZOMS_T.Check(found != "", "a vanilla type with a ladder was found (tried " + tried.ToString() + ")");
		DZOMS_T.Log("ladder type: " + found);
		DZOMS_NetworkRules.ClearTokenCache();
	}
}
