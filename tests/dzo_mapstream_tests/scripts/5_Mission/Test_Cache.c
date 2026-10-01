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

	static string Hashes(array<ref DZOMS_Batch> list)
	{
		string s = "";
		for (int i = 0; i < list.Count(); i++)
		{
			s += DZOMS_BatchHasher.Hash(list.Get(i)) + ";";
		}
		return s;
	}

	// writes header + batches by hand: version, claimed count, then 'written' batches
	static void WriteRaw(string path, int version, int claimed, array<ref DZOMS_Batch> list, int written)
	{
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.WRITE))
		{
			return;
		}
		ser.Write(version);
		ser.Write(claimed);
		for (int i = 0; i < written; i++)
		{
			ser.Write(list.Get(i));
		}
		ser.Close();
	}

	static void ExpectRejected(string path, string what)
	{
		array<ref DZOMS_Batch> res = new array<ref DZOMS_Batch>();
		res.Insert(new DZOMS_Batch());
		DZOMS_T.Check(!DZOMS_Cache.Read(path, res), what + " rejected");
		DZOMS_T.EqInt(res.Count(), 0, what + " leaves no batches");
	}

	static void Run()
	{
		DZOMS_T.Suite("cache");
		string dir = DZOMS_T.DIR + "/cache";
		MakeDirectory(DZOMS_T.DIR);
		MakeDirectory(dir);
		string path = dir + "/a.bin";
		DeleteFile(path);

		array<ref DZOMS_Batch> src = Three();
		DZOMS_T.Check(DZOMS_Cache.Write(path, src), "write succeeds");
		DZOMS_T.Check(FileExist(path), "file exists");
		array<ref DZOMS_Batch> back = new array<ref DZOMS_Batch>();
		DZOMS_T.Check(DZOMS_Cache.Read(path, back), "read succeeds");
		DZOMS_T.EqInt(back.Count(), 3, "read count");
		DZOMS_T.Eq(Hashes(back), Hashes(src), "hashes survive the round trip");
		if (back.Count() == 3)
		{
			DZOMS_T.EqInt(back.Get(2).index, 2, "index preserved");

			back.Get(1).positions.Set(0, back.Get(1).positions.Get(0) + "0.01 0 0");
			array<string> a = new array<string>();
			array<string> b = new array<string>();
			for (int i = 0; i < 3; i++)
			{
				a.Insert(DZOMS_BatchHasher.Hash(src.Get(i)));
				b.Insert(DZOMS_BatchHasher.Hash(back.Get(i)));
			}
			DZOMS_T.Check(a.Get(0) == b.Get(0) && a.Get(2) == b.Get(2), "untouched batches still match");
			DZOMS_T.Check(a.Get(1) != b.Get(1), "1 cm change mismatches exactly one batch");
		}

		ExpectRejected(dir + "/missing.bin", "missing file");

		WriteRaw(dir + "/ver.bin", DZOMS_Rpc.CACHE_VERSION + 1, 3, src, 3);
		ExpectRejected(dir + "/ver.bin", "version + 1");
		WriteRaw(dir + "/short.bin", DZOMS_Rpc.CACHE_VERSION, 3, src, 2);
		ExpectRejected(dir + "/short.bin", "3 claimed, 2 written");
		WriteRaw(dir + "/neg.bin", DZOMS_Rpc.CACHE_VERSION, -5, src, 3);
		ExpectRejected(dir + "/neg.bin", "negative count");

		array<ref DZOMS_Batch> bad = Three();
		bad.Get(1).flags.RemoveOrdered(0);
		WriteRaw(dir + "/invalid.bin", DZOMS_Rpc.CACHE_VERSION, 3, bad, 3);
		ExpectRejected(dir + "/invalid.bin", "structurally invalid batch");

		FileHandle fh = OpenFile(dir + "/random.bin", FileMode.WRITE);
		if (fh != 0)
		{
			FPrint(fh, "this is not a cache file, just some random text 12345 \x01\x02 abc");
			CloseFile(fh);
		}
		ExpectRejected(dir + "/random.bin", "random text");

		fh = OpenFile(dir + "/empty.bin", FileMode.WRITE);
		if (fh != 0)
		{
			CloseFile(fh);
		}
		ExpectRejected(dir + "/empty.bin", "empty file");

		DZOMS_T.Check(!DZOMS_Cache.Write(dir + "/nodir/x.bin", src), "write to a missing directory fails cleanly");
	}
}
