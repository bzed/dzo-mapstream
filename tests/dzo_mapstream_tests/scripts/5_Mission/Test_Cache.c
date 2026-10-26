class DZOMS_Test_Cache
{
	static array<ref DZOMS_Batch> Three()
	{
		array<ref DZOMS_Batch> list = new array<ref DZOMS_Batch>();
		for (int i = 0; i < 3; i++)
		{
			DZOMS_Batch b = DZOMS_Test_Hasher.Reference();
			b.Add("Apple", Vector(i, 2, 3), "0 0 0", 1, 0);
			b.index = i;
			list.Insert(b);
		}
		return list;
	}

	// the hash words of Three()
	static array<ref array<int>> Words3()
	{
		array<ref DZOMS_Batch> batches = Three();
		array<ref array<int>> list = new array<ref array<int>>();
		for (int i = 0; i < batches.Count(); i++)
		{
			list.Insert(DZOMS_BatchHasher.Words(batches.Get(i)));
		}
		return list;
	}

	static array<int> Key()
	{
		return DZOMS_Test_ClientDownload.TestKey();
	}

	static array<int> Nonce()
	{
		return {1000, -2000, 3000};
	}

	// the plaintext of a cache file: check word, batch count, then per batch its word count and words
	static array<int> Plain(array<ref array<int>> list)
	{
		array<int> p = new array<int>();
		p.Insert(DZOMS_Cache.CHECK_WORD);
		p.Insert(list.Count());
		for (int i = 0; i < list.Count(); i++)
		{
			p.Insert(list.Get(i).Count());
			p.InsertAll(list.Get(i));
		}
		return p;
	}

	// writes version, nonce and the plaintext encrypted with key, like DZOMS_Cache.Write
	static void WriteRaw(string path, int version, array<int> nonce, array<int> plaintext, array<int> key)
	{
		array<int> cipher = new array<int>();
		cipher.InsertAll(plaintext);
		DZOMS_ChaCha20.XorWords(key, nonce, cipher);
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.WRITE))
		{
			return;
		}
		ser.Write(version);
		ser.Write(nonce.Get(0));
		ser.Write(nonce.Get(1));
		ser.Write(nonce.Get(2));
		ser.Write(cipher);
		ser.Close();
	}

	static void ExpectRejected(string path, array<int> key, string what)
	{
		array<int> words = {7, 7};
		array<int> starts = {7};
		array<int> counts = {7};
		DZOMS_T.Check(!DZOMS_Cache.Read(path, key, words, starts, counts), what + " rejected");
		DZOMS_T.EqInt(words.Count() + starts.Count() + counts.Count(), 0, what + " leaves the arrays empty");
	}

	static int Reason(string path, array<int> key)
	{
		array<int> words = new array<int>();
		array<int> starts = new array<int>();
		array<int> counts = new array<int>();
		int reason;
		DZOMS_Cache.Read(path, key, words, starts, counts, reason);
		return reason;
	}

	static bool RangeEquals(array<int> words, int start, int count, array<int> want)
	{
		if (count != want.Count())
		{
			return false;
		}
		for (int i = 0; i < count; i++)
		{
			if (words.Get(start + i) != want.Get(i))
			{
				return false;
			}
		}
		return true;
	}

	static void Run()
	{
		DZOMS_T.Suite("cache");
		string dir = DZOMS_T.DIR + "/cache";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(dir);
		string path = dir + "/a.bin";
		DeleteFile(path);

		array<ref array<int>> src = Words3();
		DZOMS_T.Check(DZOMS_Cache.Write(path, Key(), Nonce(), src), "write succeeds");
		DZOMS_T.Check(FileExist(path), "file exists");
		array<int> words = new array<int>();
		array<int> starts = new array<int>();
		array<int> counts = new array<int>();
		DZOMS_T.Check(DZOMS_Cache.Read(path, Key(), words, starts, counts), "read succeeds");
		DZOMS_T.EqInt(starts.Count(), 3, "read count");
		DZOMS_T.EqInt(counts.Count(), 3, "read counts");
		if (starts.Count() == 3 && counts.Count() == 3)
		{
			for (int i = 0; i < 3; i++)
			{
				DZOMS_T.Check(RangeEquals(words, starts.Get(i), counts.Get(i), src.Get(i)), "batch " + i.ToString() + " words survive the round trip");
			}
		}

		array<int> otherKey = {1, 2, 3, 4, 5, 6, 7, 8};
		ExpectRejected(path, otherKey, "wrong key");
		DZOMS_T.EqInt(Reason(path, otherKey), DZOMS_Cache.REASON_KEY, "wrong key reason");
		DZOMS_T.EqInt(Reason(path, Key()), DZOMS_Cache.REASON_OK, "good file reason");

		// one flipped word inside batch 1: the file reads, only that batch's hash differs
		array<int> plain = Plain(src);
		int victim = 2 + (1 + src.Get(0).Count()) + 1 + src.Get(1).Count() - 3;
		plain.Set(victim, plain.Get(victim) ^ 4096);
		WriteRaw(dir + "/flip.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), plain, Key());
		DZOMS_T.Check(DZOMS_Cache.Read(dir + "/flip.bin", Key(), words, starts, counts), "flipped word: read succeeds");
		if (starts.Count() == 3)
		{
			string h0 = DZOMS_Sha256.HashWords(words, starts.Get(0), counts.Get(0));
			string h1 = DZOMS_Sha256.HashWords(words, starts.Get(1), counts.Get(1));
			string h2 = DZOMS_Sha256.HashWords(words, starts.Get(2), counts.Get(2));
			DZOMS_T.Eq(h0, DZOMS_Sha256.HashWords(src.Get(0)), "flipped word: batch 0 intact");
			DZOMS_T.Check(h1 != DZOMS_Sha256.HashWords(src.Get(1)), "flipped word: batch 1 differs");
			DZOMS_T.Eq(h2, DZOMS_Sha256.HashWords(src.Get(2)), "flipped word: batch 2 intact");
		}

		// rejected, without script errors
		ExpectRejected(dir + "/missing.bin", Key(), "missing file");
		DZOMS_T.EqInt(Reason(dir + "/missing.bin", Key()), DZOMS_Cache.REASON_MISSING, "missing file reason");

		FileSerializer old = new FileSerializer();
		if (old.Open(dir + "/v1.bin", FileMode.WRITE))
		{
			array<ref DZOMS_Batch> three = Three();
			old.Write(1);
			old.Write(3);
			old.Write(three.Get(0));
			old.Write(three.Get(1));
			old.Write(three.Get(2));
			old.Close();
		}
		ExpectRejected(dir + "/v1.bin", Key(), "version 1 file");
		DZOMS_T.EqInt(Reason(dir + "/v1.bin", Key()), DZOMS_Cache.REASON_VERSION, "version 1 reason");

		WriteRaw(dir + "/ver3.bin", DZOMS_Rpc.CACHE_VERSION + 1, Nonce(), plain, Key());
		ExpectRejected(dir + "/ver3.bin", Key(), "version + 1");

		FileSerializer head = new FileSerializer();
		if (head.Open(dir + "/head.bin", FileMode.WRITE))
		{
			head.Write(DZOMS_Rpc.CACHE_VERSION);
			head.Write(1);
			head.Write(2);
			head.Write(3);
			head.Close();
		}
		ExpectRejected(dir + "/head.bin", Key(), "header only");

		array<int> claim4 = Plain(src);
		claim4.Set(1, 4);
		WriteRaw(dir + "/claim4.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), claim4, Key());
		ExpectRejected(dir + "/claim4.bin", Key(), "4 batches claimed, 3 present");
		DZOMS_T.EqInt(Reason(dir + "/claim4.bin", Key()), DZOMS_Cache.REASON_BROKEN, "claimed count reason");

		array<int> toolong = Plain(src);
		toolong.Set(2, 100000);
		WriteRaw(dir + "/toolong.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), toolong, Key());
		ExpectRejected(dir + "/toolong.bin", Key(), "word count past the end");

		array<int> negwords = Plain(src);
		negwords.Set(2, -1);
		WriteRaw(dir + "/negwords.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), negwords, Key());
		ExpectRejected(dir + "/negwords.bin", Key(), "negative word count");

		array<int> extra = Plain(src);
		extra.Insert(42);
		WriteRaw(dir + "/extra.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), extra, Key());
		ExpectRejected(dir + "/extra.bin", Key(), "a word left over");

		array<int> negcount = Plain(src);
		negcount.Set(1, -5);
		WriteRaw(dir + "/negcount.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), negcount, Key());
		ExpectRejected(dir + "/negcount.bin", Key(), "negative batch count");

		array<int> hugecount = Plain(src);
		hugecount.Set(1, DZOMS_Cache.MAX_BATCHES + 1);
		WriteRaw(dir + "/hugecount.bin", DZOMS_Rpc.CACHE_VERSION, Nonce(), hugecount, Key());
		ExpectRejected(dir + "/hugecount.bin", Key(), "batch count over the limit");

		FileSerializer rnd = new FileSerializer();
		if (rnd.Open(dir + "/random.bin", FileMode.WRITE))
		{
			for (int r = 0; r < 50; r++)
			{
				rnd.Write(Math.RandomInt(3, int.MAX));
			}
			rnd.Close();
		}
		ExpectRejected(dir + "/random.bin", Key(), "50 random ints");

		FileHandle fh = OpenFile(dir + "/empty.bin", FileMode.WRITE);
		if (fh != 0)
		{
			CloseFile(fh);
		}
		ExpectRejected(dir + "/empty.bin", Key(), "empty file");

		// no batches at all is a valid file
		array<ref array<int>> none = new array<ref array<int>>();
		DZOMS_T.Check(DZOMS_Cache.Write(dir + "/none.bin", Key(), Nonce(), none), "write of 0 batches");
		DZOMS_T.Check(DZOMS_Cache.Read(dir + "/none.bin", Key(), words, starts, counts), "read of 0 batches");
		DZOMS_T.EqInt(starts.Count(), 0, "0 batches read");

		DZOMS_T.Check(!DZOMS_Cache.Write(dir + "/nodir/x.bin", Key(), Nonce(), src), "write to a missing directory fails cleanly");
	}
}
