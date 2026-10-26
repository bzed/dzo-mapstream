class DZOMS_Test_ChaCha20
{
	static array<int> RfcKey()
	{
		return {50462976, 117835012, 185207048, 252579084, 319951120, 387323156, 454695192, 522067228};
	}

	static array<int> RfcNonce()
	{
		return {150994944, 1241513984, 0};
	}

	static array<int> Data(int n)
	{
		array<int> w = new array<int>();
		for (int i = 0; i < n; i++)
		{
			w.Insert((i * 7919) & 2147483647);
		}
		return w;
	}

	static void Run()
	{
		DZOMS_T.Suite("chacha20");

		array<int> want = {-454561520, 358169553, 534581072, -999219037, -940256825, 57196595, -1700126204, 1315755203, 1180992210, 162176775, 98026004, -1576891431, -778300747, -1186064674, -394014517, 1312575650};
		array<int> dst = new array<int>();
		dst.Insert(99);
		DZOMS_ChaCha20.Block(RfcKey(), RfcNonce(), 1, dst);
		DZOMS_T.EqInt(dst.Count(), 16, "block word count");
		for (int i = 0; i < 16 && i < dst.Count(); i++)
		{
			DZOMS_T.EqInt(dst.Get(i), want.Get(i), "rfc block word " + i.ToString());
		}

		array<int> w = Data(40);
		DZOMS_ChaCha20.XorWords(RfcKey(), RfcNonce(), w);
		DZOMS_T.EqInt(w.Get(0), -40772470, "partial word 0");
		DZOMS_T.EqInt(w.Get(15), -734733720, "partial word 15");
		DZOMS_T.EqInt(w.Get(16), -454680608, "partial word 16");
		DZOMS_T.EqInt(w.Get(39), -392456093, "partial word 39");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(w), "3d0015d399bb5aa79c67df3c881875dbc7a3e6f35f75f397349f79887d781e99", "partial hash");

		DZOMS_ChaCha20.XorWords(RfcKey(), RfcNonce(), w);
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(w), DZOMS_Sha256.HashWords(Data(40)), "xor twice restores 40 words");

		array<int> none = new array<int>();
		DZOMS_ChaCha20.XorWords(RfcKey(), RfcNonce(), none);
		DZOMS_T.EqInt(none.Count(), 0, "0 words stay empty");

		array<int> three = Data(33);
		DZOMS_ChaCha20.XorWords(RfcKey(), RfcNonce(), three);
		DZOMS_T.Check(DZOMS_Sha256.HashWords(three) != DZOMS_Sha256.HashWords(Data(33)), "33 words are changed");
		DZOMS_ChaCha20.XorWords(RfcKey(), RfcNonce(), three);
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(three), DZOMS_Sha256.HashWords(Data(33)), "xor twice restores 33 words");
	}
}
