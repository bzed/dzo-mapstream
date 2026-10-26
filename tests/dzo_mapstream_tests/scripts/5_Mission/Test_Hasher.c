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

	// a position is encoded in 1/1024 m: decode + encode must give the same word at any map size
	static void PositionRoundTrip()
	{
		array<float> xs = new array<float>();
		for (int i = 0; i < 324000; i++)
		{
			xs.Insert(-20000 + i * 0.1234567);
		}
		xs.Insert(8191.999);
		xs.Insert(8192.021484375);
		xs.Insert(15359.999);
		xs.Insert(16383.999);
		xs.Insert(16384.001);
		xs.Insert(-16384.001);

		int unstable = 0;
		int inexact = 0;
		float first = 0;
		int w;
		float back;
		for (int j = 0; j < xs.Count(); j++)
		{
			w = DZOMS_BatchHasher.EncodePosition(xs.Get(j));
			back = DZOMS_BatchHasher.DecodePosition(w);
			if (DZOMS_BatchHasher.EncodePosition(back) != w)
			{
				if (unstable == 0)
				{
					first = xs.Get(j);
				}
				unstable++;
			}
			if (Math.AbsFloat(back - xs.Get(j)) > 0.0005)
			{
				inexact++;
			}
		}
		DZOMS_T.EqInt(unstable, 0, "positions whose word changes after decode + encode (first " + first.ToString() + ")");
		DZOMS_T.EqInt(inexact, 0, "positions decoded more than 0.5 mm off");
		DZOMS_T.EqInt(DZOMS_BatchHasher.EncodePosition(1000.25), 1024256, "EncodePosition 1000.25");
		DZOMS_T.EqFloat(DZOMS_BatchHasher.DecodePosition(1024256), 1000.25, "DecodePosition 1024256");
	}

	static bool SameWords(array<int> a, array<int> b)
	{
		if (a.Count() != b.Count())
		{
			return false;
		}
		for (int i = 0; i < a.Count(); i++)
		{
			if (a.Get(i) != b.Get(i))
			{
				return false;
			}
		}
		return true;
	}

	// words of the reference batch with one word replaced
	static array<int> WithWord(int index, int value)
	{
		array<int> w = DZOMS_BatchHasher.Words(Reference());
		w.Set(index, value);
		return w;
	}

	static void DecodeTests()
	{
		array<int> good = DZOMS_BatchHasher.Words(Reference());
		int n = good.Count();
		DZOMS_Batch d = DZOMS_BatchHasher.Decode(good, 0, n);
		DZOMS_T.Check(d != null, "decode reference");
		if (d)
		{
			DZOMS_T.Check(SameWords(DZOMS_BatchHasher.Words(d), good), "decode + words equal the input");
			DZOMS_T.Eq(d.GetType(0), "Land_Lighthouse", "decoded type 0");
			DZOMS_T.Eq(d.GetType(2), MODEL, "decoded type 2");
			DZOMS_T.EqInt(d.Count(), 3, "decoded count");
			DZOMS_T.Check(d.IsNetworked(0), "decoded flag");
			DZOMS_T.EqInt(d.index, 0, "decoded index is 0");
		}

		array<int> framed = new array<int>();
		framed.Insert(-5);
		framed.Insert(99);
		framed.InsertAll(good);
		framed.Insert(1);
		DZOMS_Batch inner = DZOMS_BatchHasher.Decode(framed, 2, n);
		DZOMS_T.Check(inner != null && SameWords(DZOMS_BatchHasher.Words(inner), good), "decode inside a larger array");

		DZOMS_Batch pad = new DZOMS_Batch();
		pad.Add("a", "1 2 3", "0 0 0", 1, 0);
		pad.Add("abcd", "1 2 3", "0 0 0", 1, 0);
		pad.Add("abcde", "1 2 3", "0 0 0", 1, 0);
		array<int> pw = DZOMS_BatchHasher.Words(pad);
		DZOMS_Batch pd = DZOMS_BatchHasher.Decode(pw, 0, pw.Count());
		DZOMS_T.Check(pd != null, "decode types of 1, 4 and 5 characters");
		if (pd)
		{
			DZOMS_T.Eq(pd.GetType(0), "a", "1 character");
			DZOMS_T.Eq(pd.GetType(1), "abcd", "4 characters");
			DZOMS_T.Eq(pd.GetType(2), "abcde", "5 characters");
		}

		DZOMS_T.Check(DZOMS_BatchHasher.Decode(good, 0, n - 1) == null, "one word short");
		array<int> longer = new array<int>();
		longer.InsertAll(good);
		longer.Insert(0);
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(longer, 0, n + 1) == null, "one word too many");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(1, 1000), 0, n) == null, "type length past the words");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(1, 0), 0, n) == null, "type length 0");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(1, -3), 0, n) == null, "negative type length");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(0, -1), 0, n) == null, "negative type count");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(0, 5000), 0, n) == null, "type count past the words");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(good, 0, -1) == null, "negative count");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(good, -1, n) == null, "negative start");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(good, 0, 0) == null, "no words");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(15, 2), 0, n) == null, "object count too small");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(15, 4), 0, n) == null, "object count too big");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(15, -1), 0, n) == null, "negative object count");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(15, 2147483647), 0, n) == null, "huge object count");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(16, 2), 0, n) == null, "typeIndex == type count");
		DZOMS_T.Check(DZOMS_BatchHasher.Decode(WithWord(16, -1), 0, n) == null, "negative typeIndex");

		DZOMS_Batch far = new DZOMS_Batch();
		far.Add(MODEL, "8192.021484375 8192.021484375 8192.021484375", "10 20 30", 1.25, 0);
		far.Add(MODEL, "15359.999 15359.999 15359.999", "-45.5 1.25 0", 0.75, 1);
		far.Add(MODEL, "-16384.001 -16384.001 -16384.001", "0 0 0", 1, 2);
		far.Add(MODEL, "8192.021484375 15359.999 -16384.001", "0 0 0", 1, 0);
		far.Add(MODEL, "-16384.001 8192.021484375 15359.999", "0 0 0", 1, 0);
		array<int> fw = DZOMS_BatchHasher.Words(far);
		DZOMS_Batch fd = DZOMS_BatchHasher.Decode(fw, 0, fw.Count());
		DZOMS_T.Check(fd != null && SameWords(DZOMS_BatchHasher.Words(fd), fw), "positions beyond 8192 m survive decode + words");
	}

	static void Run()
	{
		DZOMS_T.Suite("hasher");
		DecodeTests();
		PositionRoundTrip();
		DZOMS_Batch b = Reference();
		DZOMS_T.EqInt(MODEL.Length(), 32, "model path length");
		DZOMS_T.EqInt(b.Count(), 3, "count");
		DZOMS_T.EqInt(b.types.Count(), 2, "type slot reused");
		DZOMS_T.Check(b.IsNetworked(0), "entry 0 networked");
		DZOMS_T.Check(!b.IsNetworked(1), "entry 1 local");
		DZOMS_T.Eq(b.GetType(2), MODEL, "GetType");
		DZOMS_T.Check(b.IsValid(), "reference valid");

		DZOMS_T.Eq(WordsToString(DZOMS_BatchHasher.Words(b)), "2 15 1281453668 1598843239 1752459375 1970496768 32 1685740656 1818324596 1935438962 1701141620 1601202531 1700880738 1768256358 779105124 3 0 1 1024256 51712 2048768 90000 0 0 10000 1 0 1034240 52224 2058240 -45500 1250 0 15000 1 0 1044608 53248 2068480 0 0 0 7500", "reference words");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "5f0b2b4675c08a9693c5de0846889c341561d718a1a28f6538cfe28e0eb0687e", "reference hash");
		b.index = 17;
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "5f0b2b4675c08a9693c5de0846889c341561d718a1a28f6538cfe28e0eb0687e", "index is not hashed");

		b = Reference();
		b.positions.Set(2, "1020.140625 52 2020");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "eb4f272f94a6439ddb5241cc925972b73afe511668018ddc679fc356afaec7c2", "moved entry");
		b = Reference();
		b.flags.Set(1, 1);
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "18644b47438c0ba89349fa23dc0f66fb1641a1a9bd0a80d8b97466961075442d", "re-flagged entry");
		b = Reference();
		b.types.Set(0, "Land_Lighthousf");
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "030595488b7987e72092effda799c3d3a28eeeecf95d397960644a722398d0f3", "renamed type");
		b = Reference();
		b.typeIndex.RemoveOrdered(2);
		b.flags.RemoveOrdered(2);
		b.positions.RemoveOrdered(2);
		b.orientations.RemoveOrdered(2);
		b.scales.RemoveOrdered(2);
		DZOMS_T.Eq(DZOMS_BatchHasher.Hash(b), "0134ddbf04b4186b7f0485af733194000e976716e09af23d5f03a0723cac3bf1", "removed entry");

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
