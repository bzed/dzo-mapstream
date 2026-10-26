// Batch -> list of 32-bit words (DESIGN.md 10.1) -> SHA-256. The caller guarantees IsValid().
class DZOMS_BatchHasher
{
	// Positions are hashed in 1/1024 m. A power of two makes decode + encode exact at any map size
	// (a word is < 2^23 below 8192 m and x * 1024 is an integer above), which the cache relies on.
	static const int POSITION_SCALE = 1024;

	// length, then the characters packed 4 per word (big endian, zero padded)
	static void AppendString(array<int> words, string s)
	{
		int len = s.Length();
		words.Insert(len);
		int word = 0;
		int filled = 0;
		for (int i = 0; i < len; i++)
		{
			word = (word << 8) | (s.Get(i).ToAscii() & 255);
			filled++;
			if (filled == 4)
			{
				words.Insert(word);
				word = 0;
				filled = 0;
			}
		}
		if (filled > 0)
		{
			word = word << (8 * (4 - filled));
			words.Insert(word);
		}
	}

	static int EncodePosition(float v)
	{
		return Math.Round(v * POSITION_SCALE);
	}

	static float DecodePosition(int w)
	{
		return w / 1024.0;
	}

	protected static int Milli(float v)
	{
		return Math.Round(v * 1000);
	}

	static array<int> Words(DZOMS_Batch b)
	{
		array<int> w = new array<int>();
		int typeCount = b.types.Count();
		w.Insert(typeCount);
		for (int t = 0; t < typeCount; t++)
		{
			AppendString(w, b.types.Get(t));
		}
		int n = b.Count();
		w.Insert(n);
		vector p;
		vector o;
		for (int i = 0; i < n; i++)
		{
			p = b.positions.Get(i);
			o = b.orientations.Get(i);
			w.Insert(b.typeIndex.Get(i));
			w.Insert(b.flags.Get(i));
			w.Insert(EncodePosition(p[0]));
			w.Insert(EncodePosition(p[1]));
			w.Insert(EncodePosition(p[2]));
			w.Insert(Milli(o[0]));
			w.Insert(Milli(o[1]));
			w.Insert(Milli(o[2]));
			w.Insert(Math.Round(b.scales.Get(i) * 10000));
		}
		return w;
	}

	// Inverse of Words() for the range words[start .. start + count): NULL unless that range is
	// exactly one batch (every count and length is checked against the words left). index stays 0.
	static DZOMS_Batch Decode(array<int> words, int start, int count)
	{
		if (start < 0 || count < 2 || start + count > words.Count())
		{
			return null;
		}
		int end = start + count;
		int pos = start;
		DZOMS_Batch b = new DZOMS_Batch();

		int typeCount = words.Get(pos);
		pos++;
		if (typeCount < 0 || typeCount > end - pos)
		{
			return null;
		}
		int len;
		int wordCount;
		string name;
		int word;
		for (int t = 0; t < typeCount; t++)
		{
			if (pos >= end)
			{
				return null;
			}
			len = words.Get(pos);
			pos++;
			if (len < 1 || len > 4 * (end - pos))
			{
				return null;
			}
			wordCount = (len + 3) / 4;
			name = "";
			for (int k = 0; k < len; k++)
			{
				word = words.Get(pos + (k >> 2));
				name += ((word >> (24 - 8 * (k & 3))) & 255).AsciiToString();
			}
			pos += wordCount;
			b.types.Insert(name);
		}

		if (pos >= end)
		{
			return null;
		}
		int n = words.Get(pos);
		pos++;
		if (n < 0 || n > (end - pos) / 9 || n * 9 != end - pos)
		{
			return null;
		}
		for (int i = 0; i < n; i++)
		{
			b.typeIndex.Insert(words.Get(pos));
			b.flags.Insert(words.Get(pos + 1));
			b.positions.Insert(Vector(DecodePosition(words.Get(pos + 2)), DecodePosition(words.Get(pos + 3)), DecodePosition(words.Get(pos + 4))));
			b.orientations.Insert(Vector(words.Get(pos + 5) / 1000.0, words.Get(pos + 6) / 1000.0, words.Get(pos + 7) / 1000.0));
			b.scales.Insert(words.Get(pos + 8) / 10000.0);
			pos += 9;
		}
		if (!b.IsValid())
		{
			return null;
		}
		return b;
	}

	static string Hash(DZOMS_Batch b)
	{
		return DZOMS_Sha256.HashWords(Words(b));
	}

	// one hash over all batch hashes; used to compare server runs
	static string Digest(array<string> hashes)
	{
		array<int> w = new array<int>();
		w.Insert(hashes.Count());
		for (int i = 0; i < hashes.Count(); i++)
		{
			AppendString(w, hashes.Get(i));
		}
		return DZOMS_Sha256.HashWords(w);
	}
}
