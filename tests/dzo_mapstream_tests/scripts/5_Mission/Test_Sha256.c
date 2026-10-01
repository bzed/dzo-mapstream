class DZOMS_Test_Sha256
{
	static array<int> Seq(int n)
	{
		array<int> w = new array<int>();
		for (int i = 1; i <= n; i++)
		{
			w.Insert(i);
		}
		return w;
	}

	static array<int> Bytes(string s)
	{
		array<int> b = new array<int>();
		for (int i = 0; i < s.Length(); i++)
		{
			b.Insert(s.Get(i).ToAscii());
		}
		return b;
	}

	static string Repeat(string c, int n)
	{
		string s = "";
		for (int i = 0; i < n; i++)
		{
			s += c;
		}
		return s;
	}

	static void Run()
	{
		DZOMS_T.Suite("sha256");
		string empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

		array<int> w = new array<int>();
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(w), empty, "words []");
		w.Insert(1633837824);
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(w), "dc1114cd074914bd872cc1f9a23ec910ea2203bc79779ab2e17da25782a624fc", "words abc");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(1)), "b40711a88c7039756fb8a73827eabe2c0fe5a0346ca7e0a104adc0fc764f528d", "words 1..1");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(13)), "8a86417e98eb2cbe494407851dc18351326f155d54e1099475c120babe9dfd4b", "words 1..13");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(14)), "dd4821522337506fa727f0c8de421dfb5be9f9c43b868da32704869feee2b93b", "words 1..14");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(15)), "a623c527f0cb3f474b35fce6c0cc885aadfd48db31180aab5046b9f86e6cc676", "words 1..15");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(16)), "dbd03ea5a0ea94144594b68ec08063af1ae32b7598a2e4f5654c9fa7786efc42", "words 1..16");
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(Seq(17)), "b356e65c1a376b9bcf7cc8994e70323dd823699e1e67ef610df2f63ab4bdce7c", "words 1..17");

		array<int> edge = new array<int>();
		edge.Insert(-1);
		edge.Insert(1 << 31);
		edge.Insert(2147483647);
		edge.Insert(0);
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(edge), "2981f521c4ff2baf45679be9b4460d211114bcbe1ff54230eb50a324542eb450", "words with sign edge cases");

		array<int> big = new array<int>();
		for (int i = 0; i < 1000; i++)
		{
			big.Insert((i * 7919) & 2147483647);
		}
		DZOMS_T.Eq(DZOMS_Sha256.HashWords(big), "b81c1ebb6bffb62fc252449cea079ace627a985b242997cd0338995e84dbcb03", "words 1000 values");

		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes("")), empty, "bytes empty");
		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "bytes abc");
		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "bytes 56 char vector");
		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes(Repeat("a", 55))), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318", "bytes 55 x a");
		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes(Repeat("a", 56))), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a", "bytes 56 x a");
		DZOMS_T.Eq(DZOMS_Sha256.HashBytes(Bytes(Repeat("a", 64))), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "bytes 64 x a");

		DZOMS_T.EqInt(DZOMS_Sha256.Shr(-1, 28), 15, "Shr(-1, 28)");
		DZOMS_T.EqInt(DZOMS_Sha256.Shr(1 << 31, 31), 1, "Shr(1<<31, 31)");
		DZOMS_T.EqInt(DZOMS_Sha256.Shr(256, 4), 16, "Shr(256, 4)");
	}
}
