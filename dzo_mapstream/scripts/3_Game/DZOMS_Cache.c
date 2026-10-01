// Client cache file (DESIGN.md 10.2): int version, int batchCount, DZOMS_Batch per batch.
class DZOMS_Cache
{
	const int MAX_BATCHES = 65536;

	// deletes the file if any write fails
	static bool Write(string path, array<ref DZOMS_Batch> batches)
	{
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.WRITE))
		{
			return false;
		}
		bool ok = ser.Write(DZOMS_Rpc.CACHE_VERSION);
		if (ok)
		{
			ok = ser.Write(batches.Count());
		}
		for (int i = 0; ok && i < batches.Count(); i++)
		{
			ok = ser.Write(batches.Get(i));
		}
		ser.Close();
		if (!ok)
		{
			DeleteFile(path);
		}
		return ok;
	}

	// clears outBatches; on any problem returns false and leaves it empty (no partial use)
	static bool Read(string path, array<ref DZOMS_Batch> outBatches)
	{
		outBatches.Clear();
		if (!FileExist(path))
		{
			return false;
		}
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.READ))
		{
			return false;
		}
		int version;
		int count;
		bool ok = ser.Read(version);
		if (ok)
		{
			ok = version == DZOMS_Rpc.CACHE_VERSION;
		}
		if (ok)
		{
			ok = ser.Read(count);
		}
		if (ok)
		{
			ok = count >= 0 && count <= DZOMS_Cache.MAX_BATCHES;
		}
		for (int i = 0; ok && i < count; i++)
		{
			DZOMS_Batch b = new DZOMS_Batch();
			ok = ser.Read(b);
			// a read past the end of the file succeeds but leaves b NULL
			if (ok && !b)
			{
				ok = false;
			}
			if (ok)
			{
				ok = b.IsValid();
			}
			if (ok)
			{
				outBatches.Insert(b);
			}
		}
		ser.Close();
		if (!ok)
		{
			outBatches.Clear();
		}
		return ok;
	}
}
