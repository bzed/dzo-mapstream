// ChaCha20 (RFC 8439 block function) over 32-bit int words. The cache only encrypts its own
// words, so byte order does not matter: state words are used as they are. The quarter rounds
// are written out on locals on purpose (a function call per round is several times slower);
// the lines come from tests/python/chacha_vectors.py rounds.
class DZOMS_ChaCha20
{
	// key: 8 ints, nonce: 3 ints; dst is cleared and gets the 16 keystream words
	static void Block(array<int> key, array<int> nonce, int counter, array<int> dst)
	{
		int s4 = key.Get(0);
		int s5 = key.Get(1);
		int s6 = key.Get(2);
		int s7 = key.Get(3);
		int s8 = key.Get(4);
		int s9 = key.Get(5);
		int s10 = key.Get(6);
		int s11 = key.Get(7);
		int s13 = nonce.Get(0);
		int s14 = nonce.Get(1);
		int s15 = nonce.Get(2);

		int x0 = 1634760805;
		int x1 = 857760878;
		int x2 = 2036477234;
		int x3 = 1797285236;
		int x4 = s4;
		int x5 = s5;
		int x6 = s6;
		int x7 = s7;
		int x8 = s8;
		int x9 = s9;
		int x10 = s10;
		int x11 = s11;
		int x12 = counter;
		int x13 = s13;
		int x14 = s14;
		int x15 = s15;

		for (int round = 0; round < 10; round++)
		{
			x0 += x4; x12 ^= x0; x12 = (x12 << 16) | ((x12 >> 16) & 65535);
			x8 += x12; x4 ^= x8; x4 = (x4 << 12) | ((x4 >> 20) & 4095);
			x0 += x4; x12 ^= x0; x12 = (x12 << 8) | ((x12 >> 24) & 255);
			x8 += x12; x4 ^= x8; x4 = (x4 << 7) | ((x4 >> 25) & 127);
			x1 += x5; x13 ^= x1; x13 = (x13 << 16) | ((x13 >> 16) & 65535);
			x9 += x13; x5 ^= x9; x5 = (x5 << 12) | ((x5 >> 20) & 4095);
			x1 += x5; x13 ^= x1; x13 = (x13 << 8) | ((x13 >> 24) & 255);
			x9 += x13; x5 ^= x9; x5 = (x5 << 7) | ((x5 >> 25) & 127);
			x2 += x6; x14 ^= x2; x14 = (x14 << 16) | ((x14 >> 16) & 65535);
			x10 += x14; x6 ^= x10; x6 = (x6 << 12) | ((x6 >> 20) & 4095);
			x2 += x6; x14 ^= x2; x14 = (x14 << 8) | ((x14 >> 24) & 255);
			x10 += x14; x6 ^= x10; x6 = (x6 << 7) | ((x6 >> 25) & 127);
			x3 += x7; x15 ^= x3; x15 = (x15 << 16) | ((x15 >> 16) & 65535);
			x11 += x15; x7 ^= x11; x7 = (x7 << 12) | ((x7 >> 20) & 4095);
			x3 += x7; x15 ^= x3; x15 = (x15 << 8) | ((x15 >> 24) & 255);
			x11 += x15; x7 ^= x11; x7 = (x7 << 7) | ((x7 >> 25) & 127);
			x0 += x5; x15 ^= x0; x15 = (x15 << 16) | ((x15 >> 16) & 65535);
			x10 += x15; x5 ^= x10; x5 = (x5 << 12) | ((x5 >> 20) & 4095);
			x0 += x5; x15 ^= x0; x15 = (x15 << 8) | ((x15 >> 24) & 255);
			x10 += x15; x5 ^= x10; x5 = (x5 << 7) | ((x5 >> 25) & 127);
			x1 += x6; x12 ^= x1; x12 = (x12 << 16) | ((x12 >> 16) & 65535);
			x11 += x12; x6 ^= x11; x6 = (x6 << 12) | ((x6 >> 20) & 4095);
			x1 += x6; x12 ^= x1; x12 = (x12 << 8) | ((x12 >> 24) & 255);
			x11 += x12; x6 ^= x11; x6 = (x6 << 7) | ((x6 >> 25) & 127);
			x2 += x7; x13 ^= x2; x13 = (x13 << 16) | ((x13 >> 16) & 65535);
			x8 += x13; x7 ^= x8; x7 = (x7 << 12) | ((x7 >> 20) & 4095);
			x2 += x7; x13 ^= x2; x13 = (x13 << 8) | ((x13 >> 24) & 255);
			x8 += x13; x7 ^= x8; x7 = (x7 << 7) | ((x7 >> 25) & 127);
			x3 += x4; x14 ^= x3; x14 = (x14 << 16) | ((x14 >> 16) & 65535);
			x9 += x14; x4 ^= x9; x4 = (x4 << 12) | ((x4 >> 20) & 4095);
			x3 += x4; x14 ^= x3; x14 = (x14 << 8) | ((x14 >> 24) & 255);
			x9 += x14; x4 ^= x9; x4 = (x4 << 7) | ((x4 >> 25) & 127);
		}

		dst.Clear();
		dst.Insert(x0 + 1634760805);
		dst.Insert(x1 + 857760878);
		dst.Insert(x2 + 2036477234);
		dst.Insert(x3 + 1797285236);
		dst.Insert(x4 + s4);
		dst.Insert(x5 + s5);
		dst.Insert(x6 + s6);
		dst.Insert(x7 + s7);
		dst.Insert(x8 + s8);
		dst.Insert(x9 + s9);
		dst.Insert(x10 + s10);
		dst.Insert(x11 + s11);
		dst.Insert(x12 + counter);
		dst.Insert(x13 + s13);
		dst.Insert(x14 + s14);
		dst.Insert(x15 + s15);
	}

	// in place: word i XOR keystream word i, block counter from 0
	static void XorWords(array<int> key, array<int> nonce, array<int> words)
	{
		int n = words.Count();
		array<int> ks = new array<int>();
		for (int i = 0; i < n; i++)
		{
			if ((i & 15) == 0)
			{
				Block(key, nonce, i >> 4, ks);
			}
			words.Set(i, words.Get(i) ^ ks.Get(i & 15));
		}
	}
}
