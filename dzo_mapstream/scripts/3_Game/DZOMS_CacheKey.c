// The cache key (DESIGN.md 12.4): 8 ints, set in config.json as 64 hex characters or generated once
// and kept in $profile:dzo/mapstream/cachekey.bin. Also the source of the client's nonces.
class DZOMS_CacheKey
{
	static const string FILE = "cachekey.bin";
	static const int WORDS = 8;

	// n random ints, 1 <= n <= 16. Not cryptographic quality: two Math.RandomInt per word mixed with the
	// time through the ChaCha20 block function. Never Math.Randomize(-1): it reseeds the game's generator.
	static array<int> RandomWords(int n)
	{
		array<int> seed = new array<int>();
		for (int i = 0; i < 8; i++)
		{
			seed.Insert((Math.RandomInt(0, int.MAX) << 1) ^ Math.RandomInt(0, int.MAX));
		}
		int year, month, day, hour, minute, second;
		GetYearMonthDayUTC(year, month, day);
		GetHourMinuteSecondUTC(hour, minute, second);
		array<int> nonce = {TickCount(0), year * 10000 + month * 100 + day, hour * 10000 + minute * 100 + second};
		array<int> block = new array<int>();
		DZOMS_ChaCha20.Block(seed, nonce, g_Game.GetTime(), block);
		array<int> result = new array<int>();
		for (int j = 0; j < n && j < 16; j++)
		{
			result.Insert(block.Get(j));
		}
		return result;
	}

	// exactly 64 hex characters (either case) -> 8 words, big endian; on false key is empty
	static bool Parse(string hex, array<int> key)
	{
		key.Clear();
		if (hex.Length() != 8 * WORDS)
		{
			return false;
		}
		string digits = "0123456789abcdef";
		int word = 0;
		int nibble;
		string c;
		for (int i = 0; i < hex.Length(); i++)
		{
			c = hex.Substring(i, 1);
			c.ToLower();
			nibble = digits.IndexOf(c);
			if (nibble < 0)
			{
				key.Clear();
				return false;
			}
			word = (word << 4) | nibble;
			if ((i & 7) == 7)
			{
				key.Insert(word);
				word = 0;
			}
		}
		return true;
	}

	protected static bool Usable(array<int> words)
	{
		if (words.Count() != WORDS)
		{
			return false;
		}
		for (int i = 0; i < WORDS; i++)
		{
			if (words.Get(i) != 0)
			{
				return true;
			}
		}
		return false;
	}

	// never NULL. A valid configured key is used and the file is not touched. An invalid one is
	// reported and treated as empty. Empty: the key file, or a new key written to it when the file is
	// missing, unreadable, has not 8 words or is all zero.
	static array<int> Resolve(string configured, string dir)
	{
		array<int> key = new array<int>();
		if (configured != "")
		{
			if (Parse(configured, key))
			{
				return key;
			}
			DZOMS_Log.Err("config error in cacheKey: expected 64 hex characters, using the generated key");
		}

		string path = dir + "/" + FILE;
		if (FileExist(path))
		{
			FileSerializer reader = new FileSerializer();
			if (reader.Open(path, FileMode.READ))
			{
				array<int> stored = new array<int>();
				bool ok = reader.Read(stored);
				reader.Close();
				if (ok && Usable(stored))
				{
					return stored;
				}
			}
			DZOMS_Log.Warn("cache key: " + path + " is not usable, making a new key");
		}

		key = RandomWords(WORDS);
		FileSerializer writer = new FileSerializer();
		if (writer.Open(path, FileMode.WRITE))
		{
			bool written = writer.Write(key);
			writer.Close();
			if (written)
			{
				DZOMS_Log.Info("cache key: made a new key, saved to " + path);
				return key;
			}
		}
		DZOMS_Log.Err("cache key: cannot write " + path + ", the key is used for this start only");
		return key;
	}
}
