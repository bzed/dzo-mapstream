// SHA-256 over 32-bit words (each word is hashed as 4 bytes, big endian).
// Enforce ints are 32-bit signed: + and << wrap, >> sign-extends, so every right
// shift is masked. The rotations are written out in the round loop on purpose:
// a function call per rotation makes the hash several times slower.
class DZOMS_Sha256
{
	protected static ref array<int> s_K;

	protected static void InitK()
	{
		array<int> k = {1116352408, 1899447441, -1245643825, -373957723, 961987163, 1508970993, -1841331548, -1424204075, -670586216, 310598401, 607225278, 1426881987, 1925078388, -2132889090, -1680079193, -1046744716, -459576895, -272742522, 264347078, 604807628, 770255983, 1249150122, 1555081692, 1996064986, -1740746414, -1473132947, -1341970488, -1084653625, -958395405, -710438585, 113926993, 338241895, 666307205, 773529912, 1294757372, 1396182291, 1695183700, 1986661051, -2117940946, -1838011259, -1564481375, -1474664885, -1035236496, -949202525, -778901479, -694614492, -200395387, 275423344, 430227734, 506948616, 659060556, 883997877, 958139571, 1322822218, 1537002063, 1747873779, 1955562222, 2024104815, -2067236844, -1933114872, -1866530822, -1538233109, -1090935817, -965641998};
		s_K = k;
	}

	// logical right shift, 1 <= n <= 31
	static int Shr(int x, int n)
	{
		return (x >> n) & ((1 << (32 - n)) - 1);
	}

	static string HashWords(array<int> words)
	{
		int n = words.Count();
		array<int> p = new array<int>();
		p.Reserve(n + 17);
		for (int i = 0; i < n; i++)
		{
			p.Insert(words.Get(i));
		}
		p.Insert(1 << 31);
		while ((p.Count() % 16) != 14)
		{
			p.Insert(0);
		}
		p.Insert((n >> 27) & 31);
		p.Insert(n << 5);
		return Digest(p);
	}

	// values 0..255; used by the tests with the standard byte vectors
	static string HashBytes(array<int> bytes)
	{
		int n = bytes.Count();
		array<int> b = new array<int>();
		for (int i = 0; i < n; i++)
		{
			b.Insert(bytes.Get(i) & 255);
		}
		b.Insert(128);
		while ((b.Count() % 64) != 56)
		{
			b.Insert(0);
		}
		int hi = (n >> 29) & 7;
		int lo = n << 3;
		b.Insert(0);
		b.Insert(0);
		b.Insert(0);
		b.Insert(hi);
		b.Insert((lo >> 24) & 255);
		b.Insert((lo >> 16) & 255);
		b.Insert((lo >> 8) & 255);
		b.Insert(lo & 255);

		array<int> p = new array<int>();
		for (int j = 0; j < b.Count(); j += 4)
		{
			p.Insert((b.Get(j) << 24) | (b.Get(j + 1) << 16) | (b.Get(j + 2) << 8) | b.Get(j + 3));
		}
		return Digest(p);
	}

	// p: pre-padded words, a multiple of 16
	protected static string Digest(array<int> p)
	{
		if (!s_K)
		{
			InitK();
		}
		array<int> k = s_K;

		int h0 = 1779033703;
		int h1 = -1150833019;
		int h2 = 1013904242;
		int h3 = -1521486534;
		int h4 = 1359893119;
		int h5 = -1694144372;
		int h6 = 528734635;
		int h7 = 1541459225;

		array<int> w = new array<int>();
		w.Resize(64);

		int a, b, c, d, e, f, g, h;
		int x, s0, s1, t1, t2, off, t;
		int blocks = p.Count() / 16;
		for (int blk = 0; blk < blocks; blk++)
		{
			off = blk * 16;
			for (t = 0; t < 16; t++)
			{
				w.Set(t, p.Get(off + t));
			}
			for (t = 16; t < 64; t++)
			{
				x = w.Get(t - 15);
				s0 = (((x >> 7) & 0x01FFFFFF) | (x << 25)) ^ (((x >> 18) & 0x00003FFF) | (x << 14)) ^ ((x >> 3) & 0x1FFFFFFF);
				x = w.Get(t - 2);
				s1 = (((x >> 17) & 0x00007FFF) | (x << 15)) ^ (((x >> 19) & 0x00001FFF) | (x << 13)) ^ ((x >> 10) & 0x003FFFFF);
				w.Set(t, w.Get(t - 16) + s0 + w.Get(t - 7) + s1);
			}

			a = h0;
			b = h1;
			c = h2;
			d = h3;
			e = h4;
			f = h5;
			g = h6;
			h = h7;
			for (t = 0; t < 64; t++)
			{
				s1 = (((e >> 6) & 0x03FFFFFF) | (e << 26)) ^ (((e >> 11) & 0x001FFFFF) | (e << 21)) ^ (((e >> 25) & 0x0000007F) | (e << 7));
				t1 = h + s1 + ((e & f) ^ (~e & g)) + k.Get(t) + w.Get(t);
				s0 = (((a >> 2) & 0x3FFFFFFF) | (a << 30)) ^ (((a >> 13) & 0x0007FFFF) | (a << 19)) ^ (((a >> 22) & 0x000003FF) | (a << 10));
				t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
				h = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}
			h0 += a;
			h1 += b;
			h2 += c;
			h3 += d;
			h4 += e;
			h5 += f;
			h6 += g;
			h7 += h;
		}

		return Hex(h0) + Hex(h1) + Hex(h2) + Hex(h3) + Hex(h4) + Hex(h5) + Hex(h6) + Hex(h7);
	}

	protected static string Hex(int x)
	{
		string digits = "0123456789abcdef";
		string s = "";
		for (int shift = 28; shift >= 0; shift -= 4)
		{
			s += digits.Substring((x >> shift) & 15, 1);
		}
		return s;
	}
}
