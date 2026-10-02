class DZOMS_Test_Probe
{
	static void Actions(DZOMS_ObjectProbe probe, string type, vector pos, bool want)
	{
		Object o = DZOMS_TestUtil.SpawnLocal(type, pos);
		DZOMS_T.Check(o != null, type + " spawned");
		bool got = probe.HasOwnActions(o);
		DZOMS_T.Check(got == want, "HasOwnActions(" + type + ") == " + want.ToString());
		DZOMS_TestUtil.Delete(o);
	}

	static void Fixed(DZOMS_ObjectProbe probe, string type, vector pos, bool want)
	{
		Object o = DZOMS_TestUtil.SpawnLocal(type, pos);
		DZOMS_T.Check(o != null, type + " spawned");
		bool got = probe.IsFixedItem(o);
		DZOMS_T.Check(got == want, "IsFixedItem(" + type + ") == " + want.ToString());
		DZOMS_TestUtil.Delete(o);
	}

	static void Run()
	{
		DZOMS_T.Suite("probe");
		DZOMS_NetworkRules.ClearTokenCache();
		DZOMS_ObjectProbe probe = new DZOMS_ObjectProbeWorld();
		vector at = DZOMS_TestUtil.TEST_ORIGIN + "0 0 300";

		Actions(probe, "Land_Misc_Well_Pump_Blue", at, true);
		Actions(probe, "Land_Misc_Well_Pump_Blue", at, true);	// second probe of the same type must also be true
		Actions(probe, "Land_Misc_Well_Pump_Yellow", at, true);
		Actions(probe, "Land_Underground_Panel_Lever", at, true);
		Actions(probe, "Land_House_1W10", at, false);
		Actions(probe, "Apple", at, false);
		DZOMS_T.Check(!probe.HasOwnActions(null), "HasOwnActions(NULL)");

		Fixed(probe, "TerritoryFlag", at, true);
		Fixed(probe, "Apple", at, false);
		Fixed(probe, "Rag", at, false);
		Fixed(probe, "Land_House_1W10", at, false);
		DZOMS_T.Check(!probe.IsFixedItem(null), "IsFixedItem(NULL)");

		// through the rules with the real probe
		array<string> texts = {"@actions", "@fixedItems"};
		array<ref DZOMS_Rule> rules = new array<ref DZOMS_Rule>();
		array<string> errors = new array<string>();
		DZOMS_NetworkRules.Parse(texts, rules, errors);
		array<ref DZOMS_Rule> none = new array<ref DZOMS_Rule>();
		DZOMS_NetworkRules nr = DZOMS_NetworkRules.Combine(rules, none);
		string rule;
		Object pump = DZOMS_TestUtil.SpawnLocal("Land_Misc_Well_Pump_Blue", at);
		Object flag = DZOMS_TestUtil.SpawnLocal("TerritoryFlag", at + "10 0 0");
		Object apple = DZOMS_TestUtil.SpawnLocal("Apple", at + "20 0 0");
		DZOMS_T.EqInt(nr.DecideWithObject("Land_Misc_Well_Pump_Blue", pump, probe, rule), DZOMS_NetworkRules.NETWORKED, "pump networked");
		DZOMS_T.Eq(rule, "@actions", "pump by @actions");
		DZOMS_T.EqInt(nr.DecideWithObject("TerritoryFlag", flag, probe, rule), DZOMS_NetworkRules.NETWORKED, "flag networked");
		DZOMS_T.Eq(rule, "@fixedItems", "flag by @fixedItems");
		DZOMS_T.EqInt(nr.DecideWithObject("Apple", apple, probe, rule), DZOMS_NetworkRules.LOCAL, "apple local");
		DZOMS_TestUtil.Delete(pump);
		DZOMS_TestUtil.Delete(flag);
		DZOMS_TestUtil.Delete(apple);
		DZOMS_NetworkRules.ClearTokenCache();
	}
}
