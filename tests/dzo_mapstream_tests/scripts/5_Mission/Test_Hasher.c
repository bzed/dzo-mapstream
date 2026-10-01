class DZOMS_Test_Hasher
{
	static const string MODEL = "dz\\plants\\tree\\t_piceaabiesf.p3d";

	static DZOMS_Batch Reference()
	{
		DZOMS_Batch b = new DZOMS_Batch();
		b.Add("Land_Lighthouse", "1000.25 50.5 2000.75", "90 0 0", 1, DZOMS_Batch.FLAG_NETWORKED);
		b.Add(MODEL, "1010 51 2010", "-45.5 1.25 0", 1.5, 0);
		b.Add(MODEL, "1020.125 52 2020", "0 0 0", 0.75, 0);
		return b;
	}

	static string WordsToString(array<int> w)
	{
		string s = "";
		for (int i = 0; i < w.Count(); i++)
		{
			if (i > 0)
			{
				s += " ";
			}
			s += w.Get(i).ToString();
		}
		return s;
	}

	static void Run()
	{
		DZOMS_T.Suite("hasher");
		DZOMS_Batch b = Reference();
		DZOMS_T.EqInt(MODEL.Length(), 32, "model path length");
		DZOMS_T.EqInt(b.Count(), 3, "count");
		DZOMS_T.EqInt(b.types.Count(), 2, "type slot reused");
		DZOMS_T.Check(b.IsNetworked(0), "entry 0 networked");
		DZOMS_T.Check(!b.IsNetworked(1), "entry 1 local");
		DZOMS_T.Eq(b.GetType(2), MODEL, "GetType");
		DZOMS_T.Check(b.IsValid(), "reference valid");

		DZOMS_T.Eq(WordsToString(DZOMS_BatchHasher.Words(b)), "2 15 1281453668 1598843239 1752459375 1970496768 32 1685740656 1818324596 1935438962 1701141620 1601202531 1700880738 1768256358 779105124 3 0 1 1000250 50500 2000750 90000 0 0 10000 1 0 1010000 51000 2010000 -45500 1250 0 15000 1 0 1020125 52000 2020000 0 0 0 7500", "reference words");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "15f1a247ecbe2f7f46a7c93c4203c1cb4c36bd63b472a0b9681a471b754fbe60", "reference hash");
		b.index = 17;
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "15f1a247ecbe2f7f46a7c93c4203c1cb4c36bd63b472a0b9681a471b754fbe60", "index is not hashed");

		b = Reference();
		b.positions.Set(2, "1020.140625 52 2020");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "0d5bf3838a0fecb163d4eec622693ff15f56b3b195f348958e5a5039218a2622", "moved entry");
		b = Reference();
		b.flags.Set(1, 1);
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "dc3e6317c41f072552c33a2261681ce1dbd23e08f4bca631544dbda0003cd466", "re-flagged entry");
		b = Reference();
		b.types.Set(0, "Land_Lighthousf");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "cf0a7bea5b0f25aff94cdc43d9371155c8d0279aa56192691b2164c051c6a551", "renamed type");
		b = Reference();
		b.typeIndex.RemoveOrdered(2);
		b.flags.RemoveOrdered(2);
		b.positions.RemoveOrdered(2);
		b.orientations.RemoveOrdered(2);
		b.scales.RemoveOrdered(2);
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "71dbc1aab42d2bad7b674c0cac371a9b3c2386ec0f8a28e4f95929be72961cb3", "removed entry");

		b = Reference();
		b.flags.RemoveOrdered(2);
		DZOMS_T.Check(!b.IsValid(), "shorter flags array invalid");
		b = Reference();
		b.typeIndex.Set(1, b.types.Count());
		DZOMS_T.Check(!b.IsValid(), "typeIndex == types.Count() invalid");
		b = Reference();
		b.typeIndex.Set(1, -1);
		DZOMS_T.Check(!b.IsValid(), "negative typeIndex invalid");
		b = Reference();
		b.types.Set(0, "");
		DZOMS_T.Check(!b.IsValid(), "empty type invalid");
		b = Reference();
		b.scales = null;
		DZOMS_T.Check(!b.IsValid(), "NULL array invalid");
		b = Reference();
		b.positions.RemoveOrdered(0);
		DZOMS_T.Check(!b.IsValid(), "shorter positions invalid");

		DZOMS_Manifest m = new DZOMS_Manifest();
		DZOMS_T.EqInt(m.protocolVersion, DZOMS_Rpc.PROTOCOL_VERSION, "protocol version set");
		m.hashes.Insert("aa");
		m.bounds.Insert(1);
		m.bounds.Insert(2);
		m.bounds.Insert(3);
		DZOMS_T.Check(!m.IsValid(), "3 bounds for 1 hash invalid");
		m.bounds.Insert(4);
		DZOMS_T.Check(m.IsValid(), "4 bounds for 1 hash valid");
		DZOMS_T.EqInt(m.Count(), 1, "manifest count");
		DZOMS_T.Check(vector.Distance(m.GetCenter(0), "1 2 3") < 0.001, "GetCenter");
		DZOMS_T.EqFloat(m.GetRadius(0), 4, "GetRadius");

		array<string> ab = {"aa", "bb"};
		array<string> ba = {"bb", "aa"};
		string da = DZOMS_BatchHasher.Digest(ab);
		DZOMS_T.EqInt(da.Length(), 64, "digest length");
		DZOMS_T.Check(da != DZOMS_BatchHasher.Digest(ba), "digest depends on order");
	}
}
