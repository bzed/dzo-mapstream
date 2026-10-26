#!/usr/bin/env python3
"""Reference values for the Enforce ChaCha20 tests (RFC 8439 block function on int words).

python3 tests/python/chacha_vectors.py          print the reference values
python3 tests/python/chacha_vectors.py rounds   print the Enforce lines of one double round
"""
import hashlib
import struct
import sys

M = 0xFFFFFFFF


def signed(x):
    return x - (1 << 32) if x >= (1 << 31) else x


def rotl(x, n):
    return ((x << n) | (x >> (32 - n))) & M


def quarter(x, a, b, c, d):
    x[a] = (x[a] + x[b]) & M
    x[d] = rotl(x[d] ^ x[a], 16)
    x[c] = (x[c] + x[d]) & M
    x[b] = rotl(x[b] ^ x[c], 12)
    x[a] = (x[a] + x[b]) & M
    x[d] = rotl(x[d] ^ x[a], 8)
    x[c] = (x[c] + x[d]) & M
    x[b] = rotl(x[b] ^ x[c], 7)


def block(key, nonce, counter):
    """key: 8 words, nonce: 3 words (unsigned or signed), returns 16 unsigned words."""
    init = [1634760805, 857760878, 2036477234, 1797285236]
    init += [k & M for k in key] + [counter & M] + [n & M for n in nonce]
    x = list(init)
    for _ in range(10):
        quarter(x, 0, 4, 8, 12)
        quarter(x, 1, 5, 9, 13)
        quarter(x, 2, 6, 10, 14)
        quarter(x, 3, 7, 11, 15)
        quarter(x, 0, 5, 10, 15)
        quarter(x, 1, 6, 11, 12)
        quarter(x, 2, 7, 8, 13)
        quarter(x, 3, 4, 9, 14)
    return [(x[i] + init[i]) & M for i in range(16)]


def xor_words(key, nonce, words):
    out = []
    for i, w in enumerate(words):
        if i % 16 == 0:
            ks = block(key, nonce, i // 16)
        out.append(signed((w & M) ^ ks[i % 16]))
    return out


def words_hash(words):
    data = b"".join(struct.pack(">I", w & M) for w in words)
    return hashlib.sha256(data).hexdigest()


RFC_KEY = [struct.unpack("<I", bytes(range(i, i + 4)))[0] for i in range(0, 32, 4)]
RFC_NONCE = [struct.unpack("<I", bytes.fromhex("000000090000004a00000000")[i:i + 4])[0] for i in (0, 4, 8)]


def rounds():
    def q(a, b, c, d):
        r = lambda v, n: "(%s << %d) | ((%s >> %d) & %d)" % (v, n, v, 32 - n, (1 << n) - 1)
        print("x%d += x%d; x%d ^= x%d; x%d = %s;" % (a, b, d, a, d, r("x%d" % d, 16)))
        print("x%d += x%d; x%d ^= x%d; x%d = %s;" % (c, d, b, c, b, r("x%d" % b, 12)))
        print("x%d += x%d; x%d ^= x%d; x%d = %s;" % (a, b, d, a, d, r("x%d" % d, 8)))
        print("x%d += x%d; x%d ^= x%d; x%d = %s;" % (c, d, b, c, b, r("x%d" % b, 7)))
    for a, b, c, d in ((0, 4, 8, 12), (1, 5, 9, 13), (2, 6, 10, 14), (3, 7, 11, 15),
                       (0, 5, 10, 15), (1, 6, 11, 12), (2, 7, 8, 13), (3, 4, 9, 14)):
        q(a, b, c, d)


def table():
    print("key words:", ", ".join(str(signed(k)) for k in RFC_KEY))
    print("nonce words:", ", ".join(str(signed(n)) for n in RFC_NONCE))
    print("rfc block:", ", ".join(str(signed(v)) for v in block(RFC_KEY, RFC_NONCE, 1)))
    data = [(i * 7919) & 0x7FFFFFFF for i in range(40)]
    enc = xor_words(RFC_KEY, RFC_NONCE, data)
    print("partial [0] [15] [16] [39]:", enc[0], enc[15], enc[16], enc[39])
    print("partial hash:", words_hash(enc))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "rounds":
        rounds()
    else:
        table()
