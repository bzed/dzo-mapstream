#!/usr/bin/env python3
"""Reference values for the Enforce SHA-256 tests (checked against hashlib).

python3 tests/python/sha_vectors.py            print the test table
python3 tests/python/sha_vectors.py constants  print K and H0 as signed decimals
"""
import hashlib
import struct
import sys


def signed(x):
    return x - (1 << 32) if x >= (1 << 31) else x


def words_hash(words):
    data = b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in words)
    return hashlib.sha256(data).hexdigest()


def bytes_hash(data):
    return hashlib.sha256(data).hexdigest()


def primes(n):
    out, c = [], 2
    while len(out) < n:
        if all(c % p for p in out):
            out.append(c)
        c += 1
    return out


def frac32(p, root):
    from decimal import Decimal, getcontext
    getcontext().prec = 80
    v = Decimal(p) ** (Decimal(1) / Decimal(root))
    return int((v - int(v)) * (1 << 32))


def constants():
    k = [signed(frac32(p, 3)) for p in primes(64)]
    h = [signed(frac32(p, 2)) for p in primes(8)]
    print("K:", ", ".join(str(x) for x in k))
    print("H0:", ", ".join(str(x) for x in h))


def table():
    rows = [
        ("words []", words_hash([])),
        ("words [1633837824]", words_hash([0x61626300])),
        ("words 1..1", words_hash(list(range(1, 2)))),
    ]
    for n in (13, 14, 15, 16, 17):
        rows.append(("words 1..%d" % n, words_hash(list(range(1, n + 1)))))
    rows.append(("words [-1, 1<<31, 2147483647, 0]", words_hash([-1, -(1 << 31), 2147483647, 0])))
    rows.append(("words (i*7919)&0x7fffffff i<1000", words_hash([(i * 7919) & 0x7FFFFFFF for i in range(1000)])))
    rows.append(('bytes ""', bytes_hash(b"")))
    rows.append(('bytes "abc"', bytes_hash(b"abc")))
    rows.append(("bytes abcdbcde...", bytes_hash(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")))
    for n in (55, 56, 64):
        rows.append(("bytes %d x a" % n, bytes_hash(b"a" * n)))
    for name, h in rows:
        print("%-40s %s" % (name, h))


def rnd(f):
    """Enforce Math.Round: halves away from zero."""
    import math
    return int(math.floor(abs(f) + 0.5)) * (1 if f >= 0 else -1)


def pack_string(s):
    out = [len(s)]
    raw = [ord(c) for c in s] + [0] * ((-len(s)) % 4)
    for i in range(0, len(raw), 4):
        out.append((raw[i] << 24) | (raw[i + 1] << 16) | (raw[i + 2] << 8) | raw[i + 3])
    return [signed(x) for x in out]


def batch_words(types, entries):
    """entries: (typeIndex, flags, pos, ori, scale)"""
    w = [len(types)]
    for t in types:
        w += pack_string(t)
    w.append(len(entries))
    for ti, fl, pos, ori, sc in entries:
        w += [ti, fl] + [rnd(v * 1024) for v in pos] + [rnd(v * 1000) for v in ori] + [rnd(sc * 10000)]
    return w


def batch_table():
    types = ["Land_Lighthouse", "dz\\plants\\tree\\t_piceaabiesf.p3d"]
    e = [
        (0, 1, (1000.25, 50.5, 2000.75), (90, 0, 0), 1.0),
        (1, 0, (1010, 51, 2010), (-45.5, 1.25, 0), 1.5),
        (1, 0, (1020.125, 52, 2020), (0, 0, 0), 0.75),
    ]
    print("words:", " ".join(str(x) for x in batch_words(types, e)))
    print("reference", words_hash(batch_words(types, e)))
    e2 = list(e)
    e2[2] = (1, 0, (1020.140625, 52, 2020), (0, 0, 0), 0.75)
    print("moved    ", words_hash(batch_words(types, e2)))
    e3 = list(e)
    e3[1] = (1, 1, (1010, 51, 2010), (-45.5, 1.25, 0), 1.5)
    print("flags    ", words_hash(batch_words(types, e3)))
    print("renamed  ", words_hash(batch_words(["Land_Lighthousf", types[1]], e)))
    print("removed  ", words_hash(batch_words(types, e[:2])))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "constants":
        constants()
    elif len(sys.argv) > 1 and sys.argv[1] == "batch":
        batch_table()
    else:
        table()
