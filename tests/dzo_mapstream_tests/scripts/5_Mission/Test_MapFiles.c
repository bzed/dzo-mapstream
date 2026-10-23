class DZOMS_Test_MapFiles
{
	static void Run()
	{
		DZOMS_T.Suite("mapfiles");
		string dir = DZOMS_T.DIR + "/DZE_list";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(dir);
		MakeDirectory(dir + "/sub.dze");
		string names[6] = {"b.dze", "A.dze", "c.DZE", "notes.txt", "d.dze.bak", ".dze"};
		for (int i = 0; i < 6; i++)
		{
			DZOMS_Test_Config.WriteText(dir + "/" + names[i], "x");
		}

		array<string> found;
		DZOMS_MapFiles.List(dir, found);
		DZOMS_T.EqInt(found.Count(), 3, "3 map files: other extensions and directories skipped");
		if (found.Count() == 3)
		{
			DZOMS_T.Eq(found.Get(0), "A.dze", "sorted without regard to case: first");
			DZOMS_T.Eq(found.Get(1), "b.dze", "sorted: second");
			DZOMS_T.Eq(found.Get(2), "c.DZE", "extension in any case: third");
		}

		array<string> none;
		DZOMS_MapFiles.List(DZOMS_T.DIR + "/DZE_does_not_exist", none);
		DZOMS_T.EqInt(none.Count(), 0, "missing folder lists nothing");

		// regression of the 1.30 bug (DZEXP-134, fixed in 1.30.164200): FindFile returned the server root's files for $profile: patterns
		bool rootLeak = false;
		for (int j = 0; j < found.Count(); j++)
		{
			if (found.Get(j) == "DayZServer")
			{
				rootLeak = true;
			}
		}
		DZOMS_T.Check(!rootLeak, "no server root files in the list");

		DZOMS_T.Check(DZOMS_MapFiles.IsMapFile("x.dze"), "IsMapFile x.dze");
		DZOMS_T.Check(DZOMS_MapFiles.IsMapFile("X.DzE"), "IsMapFile X.DzE");
		DZOMS_T.Check(!DZOMS_MapFiles.IsMapFile(".dze"), "IsMapFile .dze has no name");
		DZOMS_T.Check(!DZOMS_MapFiles.IsMapFile("x.dzex"), "IsMapFile x.dzex");
	}
}
