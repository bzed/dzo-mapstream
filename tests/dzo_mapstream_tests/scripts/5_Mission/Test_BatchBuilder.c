class DZOMS_Test_BatchBuilder
{
	static void Add(array<ref DZOMS_ObjectEntry> list, string type, vector pos, int n)
	{
		for (int i = 0; i < n; i++)
		{
			list.Insert(new DZOMS_ObjectEntry(type, pos + Vector(i * 0.1, 0, 0), "0 0 0", 1, 0));
		}
	}

	static string Sizes(array<ref DZOMS_Batch> batches)
	{
		string s = "";
		for (int i = 0; i < batches.Count(); i++)
		{
			if (i > 0)
			{
				s += ",";
			}
			s += batches.Get(i).Count().ToString();
		}
		return s;
	}

	static string DigestOf(array<ref DZOMS_Batch> batches)
	{
		array<string> hashes = new array<string>();
		for (int i = 0; i < batches.Count(); i++)
		{
			hashes.Insert(DZOMS_BatchHasher.Hash(batches.Get(i)));
		}
		return DZOMS_BatchHasher.Digest(hashes);
	}

	static void Case(string name, array<ref DZOMS_ObjectEntry> list, int size, string wantSizes)
	{
		array<ref DZOMS_Batch> batches = new array<ref DZOMS_Batch>();
		array<float> bounds = new array<float>();
		DZOMS_BatchBuilder.Build(list, size, batches, bounds);
		DZOMS_T.Eq(Sizes(batches), wantSizes, name + ": batch sizes");
		DZOMS_T.EqInt(bounds.Count(), 4 * batches.Count(), name + ": bounds count");
		for (int i = 0; i < batches.Count(); i++)
		{
			DZOMS_T.EqInt(batches.Get(i).index, i, name + ": index " + i.ToString());
		}
	}

	// every input object maps to a batch entry with the same type, position and orientation
	static void RunMapping()
	{
		array<ref DZOMS_ObjectEntry> list = new array<ref DZOMS_ObjectEntry>();
		Add(list, "A", "10 10 10", 30);
		Add(list, "B", "70 10 10", 20);
		Add(list, "A", "900 10 10", 25);
		Add(list, "C", "10 10 10", 60);
		array<ref DZOMS_Batch> batches = new array<ref DZOMS_Batch>();
		array<float> bounds = new array<float>();
		array<int> objBatch = new array<int>();
		array<int> objIndex = new array<int>();
		DZOMS_BatchBuilder.Build(list, 50, batches, bounds, objBatch, objIndex);
		DZOMS_T.EqInt(objBatch.Count(), list.Count(), "mapping: batch array has the input length");
		DZOMS_T.EqInt(objIndex.Count(), list.Count(), "mapping: index array has the input length");
		int bad = 0;
		map<string, bool> seen = new map<string, bool>();
		for (int i = 0; i < list.Count(); i++)
		{
			if (i >= objBatch.Count() || i >= objIndex.Count())
			{
				bad++;
				continue;
			}
			int b = objBatch.Get(i);
			int k = objIndex.Get(i);
			if (b < 0 || b >= batches.Count() || k < 0 || k >= batches.Get(b).Count())
			{
				bad++;
				continue;
			}
			DZOMS_Batch batch = batches.Get(b);
			DZOMS_ObjectEntry e = list.Get(i);
			if (batch.GetType(k) != e.type || batch.positions.Get(k) != e.pos || batch.orientations.Get(k) != e.ori)
			{
				bad++;
			}
			string key = b.ToString() + "/" + k.ToString();
			if (seen.Contains(key))
			{
				bad++;
			}
			seen.Set(key, true);
		}
		DZOMS_T.EqInt(bad, 0, "mapping: every object resolves to its own batch entry");
		DZOMS_T.EqInt(seen.Count(), list.Count(), "mapping: no two objects share an entry");

		// the old signature builds the same batches
		array<ref DZOMS_Batch> again = new array<ref DZOMS_Batch>();
		array<float> boundsAgain = new array<float>();
		DZOMS_BatchBuilder.Build(list, 50, again, boundsAgain);
		DZOMS_T.Eq(DigestOf(again), DigestOf(batches), "mapping: same batches as the four-argument build");
	}

	static void Run()
	{
		DZOMS_T.Suite("batchbuilder");
		RunMapping();
		array<ref DZOMS_ObjectEntry> l1 = new array<ref DZOMS_ObjectEntry>();
		Add(l1, "A", "10 10 10", 3);
		Add(l1, "A", "70 10 10", 1);
		Add(l1, "A", "510 10 10", 1);
		Case("two near, one far", l1, 400, "4,1");

		array<ref DZOMS_ObjectEntry> l2 = new array<ref DZOMS_ObjectEntry>();
		Add(l2, "A", "10 10 10", 120);
		Case("split big cell", l2, 50, "50,50,20");

		array<ref DZOMS_ObjectEntry> l3 = new array<ref DZOMS_ObjectEntry>();
		Add(l3, "A", "10 10 10", 40);
		Add(l3, "B", "35 10 10", 20);
		Case("does not fit", l3, 50, "40,20");

		array<ref DZOMS_ObjectEntry> l4 = new array<ref DZOMS_ObjectEntry>();
		Add(l4, "A", "10 10 10", 30);
		Add(l4, "FAR", "60 10 10", 15);
		Add(l4, "NEAR", "35 10 10", 15);
		Add(l4, "Z", "10 10 85", 2);
		array<ref DZOMS_Batch> b4 = new array<ref DZOMS_Batch>();
		array<float> bo4 = new array<float>();
		DZOMS_BatchBuilder.Build(l4, 50, b4, bo4);
		DZOMS_T.Eq(Sizes(b4), "45,17", "nearest first, stop at first misfit: sizes");
		if (b4.Count() == 2)
		{
			DZOMS_T.Check(b4.Get(0).types.Find("NEAR") >= 0, "NEAR in the first batch");
			DZOMS_T.Check(b4.Get(0).types.Find("FAR") < 0, "FAR not in the first batch");
		}

		array<ref DZOMS_ObjectEntry> l5 = new array<ref DZOMS_ObjectEntry>();
		l5.Insert(new DZOMS_ObjectEntry("A", "0 0 0", "0 0 0", 1, 0));
		l5.Insert(new DZOMS_ObjectEntry("A", "10 0 0", "0 0 0", 1, 0));
		array<ref DZOMS_Batch> b5 = new array<ref DZOMS_Batch>();
		array<float> bo5 = new array<float>();
		DZOMS_BatchBuilder.Build(l5, 400, b5, bo5);
		DZOMS_T.EqInt(b5.Count(), 1, "pair in one batch");
		if (bo5.Count() == 4)
		{
			DZOMS_T.EqFloat(bo5.Get(0), 5, "center x");
			DZOMS_T.EqFloat(bo5.Get(1), 0, "center y");
			DZOMS_T.EqFloat(bo5.Get(3), 5, "radius");
		}

		array<ref DZOMS_ObjectEntry> l6 = new array<ref DZOMS_ObjectEntry>();
		l6.Insert(new DZOMS_ObjectEntry("A", "-1 0 -1", "0 0 0", 1, 0));
		l6.Insert(new DZOMS_ObjectEntry("A", "-1 300 -1", "0 0 0", 1, 0));
		Case("negative coordinates and far in y", l6, 400, "1,1");

		array<ref DZOMS_ObjectEntry> l7 = new array<ref DZOMS_ObjectEntry>();
		l7.Insert(new DZOMS_ObjectEntry("Land_X", "5 6 7", "1 2 3", 2.5, 1));
		array<ref DZOMS_Batch> b7 = new array<ref DZOMS_Batch>();
		array<float> bo7 = new array<float>();
		DZOMS_BatchBuilder.Build(l7, 400, b7, bo7);
		if (b7.Count() == 1 && b7.Get(0).Count() == 1)
		{
			DZOMS_Batch only = b7.Get(0);
			DZOMS_T.Eq(only.GetType(0), "Land_X", "type carried");
			DZOMS_T.EqFloat(only.scales.Get(0), 2.5, "scale carried");
			DZOMS_T.EqInt(only.flags.Get(0), 1, "flags carried");
			DZOMS_T.Check(vector.Distance(only.orientations.Get(0), "1 2 3") < 0.001, "orientation carried");
			DZOMS_T.Check(vector.Distance(only.positions.Get(0), "5 6 7") < 0.001, "position carried");
		}
		else
		{
			DZOMS_T.Check(false, "single object batch");
		}

		// determinism
		array<ref DZOMS_ObjectEntry> l8 = new array<ref DZOMS_ObjectEntry>();
		array<ref DZOMS_ObjectEntry> l8r = new array<ref DZOMS_ObjectEntry>();
		for (int i = 0; i < 300; i++)
		{
			int tk = i % 7;
			int px = (i * 37) % 900;
			int pz = (i * 53) % 700;
			string tn = "T" + tk.ToString();
			vector p = Vector(px, 0, pz);
			l8.Insert(new DZOMS_ObjectEntry(tn, p, "0 0 0", 1, 0));
		}
		for (int j = l8.Count() - 1; j >= 0; j--)
		{
			l8r.Insert(l8.Get(j));
		}
		array<ref DZOMS_Batch> ba = new array<ref DZOMS_Batch>();
		array<ref DZOMS_Batch> bb = new array<ref DZOMS_Batch>();
		array<ref DZOMS_Batch> bc = new array<ref DZOMS_Batch>();
		array<float> boa = new array<float>();
		array<float> bob = new array<float>();
		array<float> boc = new array<float>();
		DZOMS_BatchBuilder.Build(l8, 50, ba, boa);
		DZOMS_BatchBuilder.Build(l8, 50, bb, bob);
		DZOMS_BatchBuilder.Build(l8r, 50, bc, boc);
		DZOMS_T.Eq(DigestOf(ba), DigestOf(bb), "same input, same digest");
		DZOMS_T.Check(DigestOf(ba) != DigestOf(bc), "reversed input, different digest");
		int total = 0;
		int biggest = 0;
		for (int k = 0; k < ba.Count(); k++)
		{
			total += ba.Get(k).Count();
			biggest = Math.Max(biggest, ba.Get(k).Count());
		}
		DZOMS_T.EqInt(total, 300, "all objects batched");
		DZOMS_T.Check(biggest <= 50, "no batch above the size");
	}
}
