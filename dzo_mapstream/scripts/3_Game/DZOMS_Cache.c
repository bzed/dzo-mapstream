// Client cache file (DESIGN.md 12.5): int version, 3 nonce ints, then one array<int> encrypted with
// ChaCha20 (the server's key): check word, batch count, per batch its word count and its hash words.
class DZOMS_Cache
{
	static const int CHECK_WORD = 1146768707;	// 0x445A4D43, a wrong value after decryption means another key
	static const int MAX_BATCHES = 65536;

	// why Read failed, for the log
	static const int REASON_OK = 0;
	static const int REASON_MISSING = 1;
	static const int REASON_VERSION = 2;
	static const int REASON_KEY = 3;
	static const int REASON_BROKEN = 4;

	// deletes the file if any write fails
	static bool Write(string path, array<int> key, array<int> nonce, array<ref array<int>> batchWords)
	{
		array<int> data = new array<int>();
		data.Insert(CHECK_WORD);
		data.Insert(batchWords.Count());
		for (int i = 0; i < batchWords.Count(); i++)
		{
			data.Insert(batchWords.Get(i).Count());
			data.InsertAll(batchWords.Get(i));
		}
		DZOMS_ChaCha20.XorWords(key, nonce, data);

		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.WRITE))
		{
			return false;
		}
		bool ok = ser.Write(DZOMS_Rpc.CACHE_VERSION);
		for (int n = 0; ok && n < 3; n++)
		{
			ok = ser.Write(nonce.Get(n));
		}
		if (ok)
		{
			ok = ser.Write(data);
		}
		ser.Close();
		if (!ok)
		{
			DeleteFile(path);
		}
		return ok;
	}

	static bool Read(string path, array<int> key, array<int> words, array<int> starts, array<int> counts)
	{
		int reason;
		return Read(path, key, words, starts, counts, reason);
	}

	// clears the three arrays; on success words is the decrypted plaintext and batch i is
	// words[starts[i] .. starts[i] + counts[i]). On any problem returns false and leaves them empty.
	static bool Read(string path, array<int> key, array<int> words, array<int> starts, array<int> counts, out int reason)
	{
		words.Clear();
		starts.Clear();
		counts.Clear();
		reason = REASON_BROKEN;
		if (!FileExist(path))
		{
			reason = REASON_MISSING;
			return false;
		}
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.READ))
		{
			return false;
		}
		int version;
		array<int> nonce = new array<int>();
		int part;
		array<int> cipher = new array<int>();
		bool ok = ser.Read(version);
		if (ok && version != DZOMS_Rpc.CACHE_VERSION)
		{
			ser.Close();
			reason = REASON_VERSION;
			return false;
		}
		for (int n = 0; ok && n < 3; n++)
		{
			ok = ser.Read(part);
			nonce.Insert(part);
		}
		if (ok)
		{
			ok = ser.Read(cipher);
		}
		ser.Close();
		// a read past the end of the file leaves the array NULL or empty: rejected here
		if (!ok || !cipher || cipher.Count() < 2)
		{
			return false;
		}
		words.InsertAll(cipher);

		DZOMS_ChaCha20.XorWords(key, nonce, words);
		if (words.Get(0) != CHECK_WORD)
		{
			words.Clear();
			reason = REASON_KEY;
			return false;
		}
		int batchCount = words.Get(1);
		int size = words.Count();
		int pos = 2;
		int wordCount;
		ok = batchCount >= 0 && batchCount <= MAX_BATCHES;
		for (int i = 0; ok && i < batchCount; i++)
		{
			ok = pos < size;
			if (ok)
			{
				wordCount = words.Get(pos);
				pos++;
				ok = wordCount >= 0 && wordCount <= size - pos;
			}
			if (ok)
			{
				starts.Insert(pos);
				counts.Insert(wordCount);
				pos += wordCount;
			}
		}
		if (ok)
		{
			ok = pos == size;
		}
		if (!ok)
		{
			words.Clear();
			starts.Clear();
			counts.Clear();
			return false;
		}
		reason = REASON_OK;
		return true;
	}
}
