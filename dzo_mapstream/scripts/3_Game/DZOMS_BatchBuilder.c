class DZOMS_ObjectEntry
{
	string type;
	vector pos;
	vector ori;
	float scale;
	int flags;

	void DZOMS_ObjectEntry(string t, vector p, vector o, float s, int f)
	{
		type = t;
		pos = p;
		ori = o;
		scale = s;
		flags = f;
	}
}

// Groups objects into batches (DESIGN.md 6.3 steps 1-4). Deterministic: cells are kept in
// order of first appearance, a map is only used for lookups, never iterated.
class DZOMS_BatchBuilder
{
	static const float CELL = 25;
	static const int NEIGHBOUR_RANGE = 4;	// cell centers within 100 m: dx^2 + dy^2 + dz^2 <= 16

	protected static string Key(int x, int y, int z)
	{
		return x.ToString() + " " + y.ToString() + " " + z.ToString();
	}

	static void Build(array<ref DZOMS_ObjectEntry> objects, int batchSize, array<ref DZOMS_Batch> outBatches, array<float> outBounds)
	{
		outBatches.Clear();
		outBounds.Clear();

		array<int> cellX = new array<int>();
		array<int> cellY = new array<int>();
		array<int> cellZ = new array<int>();
		array<ref array<int>> cellObjects = new array<ref array<int>>();
		map<string, int> lookup = new map<string, int>();

		int cx, cy, cz, index;
		string key;
		for (int i = 0; i < objects.Count(); i++)
		{
			vector p = objects.Get(i).pos;
			cx = Math.Floor(p[0] / CELL);
			cy = Math.Floor(p[1] / CELL);
			cz = Math.Floor(p[2] / CELL);
			key = Key(cx, cy, cz);
			if (!lookup.Find(key, index))
			{
				index = cellObjects.Count();
				lookup.Set(key, index);
				cellX.Insert(cx);
				cellY.Insert(cy);
				cellZ.Insert(cz);
				cellObjects.Insert(new array<int>());
			}
			cellObjects.Get(index).Insert(i);
		}

		int cellCount = cellObjects.Count();
		array<bool> used = new array<bool>();
		for (int u = 0; u < cellCount; u++)
		{
			used.Insert(false);
		}

		int dx, dy, dz, d2, other;
		for (int seed = 0; seed < cellCount; seed++)
		{
			if (used.Get(seed))
			{
				continue;
			}
			used.Set(seed, true);

			array<int> current = new array<int>();
			array<int> seedObjects = cellObjects.Get(seed);
			for (int s = 0; s < seedObjects.Count(); s++)
			{
				current.Insert(seedObjects.Get(s));
				if (current.Count() == batchSize)
				{
					Emit(objects, current, outBatches, outBounds);
					current = new array<int>();
				}
			}

			array<int> candidates = new array<int>();
			for (dx = -NEIGHBOUR_RANGE; dx <= NEIGHBOUR_RANGE; dx++)
			{
				for (dy = -NEIGHBOUR_RANGE; dy <= NEIGHBOUR_RANGE; dy++)
				{
					for (dz = -NEIGHBOUR_RANGE; dz <= NEIGHBOUR_RANGE; dz++)
					{
						d2 = dx * dx + dy * dy + dz * dz;
						if (d2 == 0 || d2 > NEIGHBOUR_RANGE * NEIGHBOUR_RANGE)
						{
							continue;
						}
						key = Key(cellX.Get(seed) + dx, cellY.Get(seed) + dy, cellZ.Get(seed) + dz);
						if (lookup.Find(key, other) && !used.Get(other))
						{
							// sort key: squared offset first, then order of first appearance
							candidates.Insert(d2 * (cellCount + 1) + other);
						}
					}
				}
			}
			candidates.Sort();

			for (int c = 0; c < candidates.Count(); c++)
			{
				other = candidates.Get(c) % (cellCount + 1);
				array<int> otherObjects = cellObjects.Get(other);
				if (current.Count() + otherObjects.Count() > batchSize)
				{
					break;
				}
				used.Set(other, true);
				for (int o = 0; o < otherObjects.Count(); o++)
				{
					current.Insert(otherObjects.Get(o));
				}
			}

			if (current.Count() > 0)
			{
				Emit(objects, current, outBatches, outBounds);
			}
		}
	}

	protected static void Emit(array<ref DZOMS_ObjectEntry> objects, array<int> members, array<ref DZOMS_Batch> outBatches, array<float> outBounds)
	{
		DZOMS_Batch b = new DZOMS_Batch();
		b.index = outBatches.Count();
		vector sum = "0 0 0";
		int n = members.Count();
		for (int i = 0; i < n; i++)
		{
			DZOMS_ObjectEntry e = objects.Get(members.Get(i));
			b.Add(e.type, e.pos, e.ori, e.scale, e.flags);
			sum = sum + e.pos;
		}
		float inv = 1.0 / n;
		vector center = sum * inv;
		float radius = 0;
		for (int j = 0; j < n; j++)
		{
			radius = Math.Max(radius, vector.Distance(center, b.positions.Get(j)));
		}
		outBatches.Insert(b);
		outBounds.Insert(center[0]);
		outBounds.Insert(center[1]);
		outBounds.Insert(center[2]);
		outBounds.Insert(radius);
	}
}
