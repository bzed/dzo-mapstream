// Transfer and cache payloads: only ints, floats, bools, strings, vectors and arrays of those.
class DZOMS_Batch
{
	const int FLAG_NETWORKED = 1;

	int index;
	ref array<string> types;
	ref array<int> typeIndex;
	ref array<int> flags;
	ref array<vector> positions;
	ref array<vector> orientations;
	ref array<float> scales;

	void DZOMS_Batch()
	{
		types = new array<string>();
		typeIndex = new array<int>();
		flags = new array<int>();
		positions = new array<vector>();
		orientations = new array<vector>();
		scales = new array<float>();
	}

	int Count()
	{
		return typeIndex.Count();
	}

	bool IsNetworked(int i)
	{
		return (flags.Get(i) & FLAG_NETWORKED) == FLAG_NETWORKED;
	}

	string GetType(int i)
	{
		return types.Get(typeIndex.Get(i));
	}

	int Add(string type, vector pos, vector ori, float scale, int entryFlags)
	{
		int slot = types.Find(type);
		if (slot < 0)
		{
			slot = types.Insert(type);
		}
		typeIndex.Insert(slot);
		flags.Insert(entryFlags);
		positions.Insert(pos);
		orientations.Insert(ori);
		scales.Insert(scale);
		return typeIndex.Count() - 1;
	}

	// structural check; must pass before a batch is hashed or spawned
	bool IsValid()
	{
		if (!types || !typeIndex || !flags || !positions || !orientations || !scales)
		{
			return false;
		}
		int n = typeIndex.Count();
		if (flags.Count() != n || positions.Count() != n || orientations.Count() != n || scales.Count() != n)
		{
			return false;
		}
		int typeCount = types.Count();
		for (int i = 0; i < typeCount; i++)
		{
			if (types.Get(i) == "")
			{
				return false;
			}
		}
		for (int j = 0; j < n; j++)
		{
			int ti = typeIndex.Get(j);
			if (ti < 0 || ti >= typeCount)
			{
				return false;
			}
		}
		return true;
	}
}

class DZOMS_Manifest
{
	int protocolVersion;
	float viewDistance;
	float proxyDistance;
	bool disableCache;
	ref array<float> bounds;	// x, y, z, radius per batch
	ref array<string> hashes;	// lower-case hex SHA-256 per batch

	void DZOMS_Manifest()
	{
		protocolVersion = DZOMS_Rpc.PROTOCOL_VERSION;
		bounds = new array<float>();
		hashes = new array<string>();
	}

	int Count()
	{
		return hashes.Count();
	}

	bool IsValid()
	{
		if (!bounds || !hashes)
		{
			return false;
		}
		return bounds.Count() == 4 * hashes.Count();
	}

	vector GetCenter(int i)
	{
		return Vector(bounds.Get(4 * i), bounds.Get(4 * i + 1), bounds.Get(4 * i + 2));
	}

	float GetRadius(int i)
	{
		return bounds.Get(4 * i + 3);
	}
}
