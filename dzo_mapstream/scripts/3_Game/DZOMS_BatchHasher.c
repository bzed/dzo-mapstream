// Batch -> list of 32-bit words (DESIGN.md 10.1) -> SHA-256. The caller guarantees IsValid().
class DZOMS_BatchHasher
{
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
			w.Insert(Milli(p[0]));
			w.Insert(Milli(p[1]));
			w.Insert(Milli(p[2]));
			w.Insert(Milli(o[0]));
			w.Insert(Milli(o[1]));
			w.Insert(Milli(o[2]));
			w.Insert(Math.Round(b.scales.Get(i) * 10000));
		}
		return w;
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
