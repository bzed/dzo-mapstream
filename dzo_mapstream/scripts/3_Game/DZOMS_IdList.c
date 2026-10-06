// Ids of objects as (batch, index inside the batch), for the networked/local announcements
// (DESIGN 13.5). Flat arrays only, like the other payload classes.
class DZOMS_IdList
{
	const int MAX_IDS = 500;

	ref array<int> batch;
	ref array<int> index;
	// network id of the networked copy, per object; only filled in OBJECTS_NETWORKED
	ref array<int> netLow;
	ref array<int> netHigh;

	void DZOMS_IdList()
	{
		batch = new array<int>();
		index = new array<int>();
		netLow = new array<int>();
		netHigh = new array<int>();
	}

	int Count()
	{
		return batch.Count();
	}

	void Add(int b, int i)
	{
		batch.Insert(b);
		index.Insert(i);
	}

	void AddNet(int b, int i, int low, int high)
	{
		batch.Insert(b);
		index.Insert(i);
		netLow.Insert(low);
		netHigh.Insert(high);
	}

	bool HasNetIds()
	{
		return batch.Count() > 0 && netLow.Count() == batch.Count() && netHigh.Count() == batch.Count();
	}

	// equal lengths and not more than MAX_IDS: what a receiver may use. The network id arrays are
	// either both empty or as long as the ids.
	bool IsValid()
	{
		if (!batch || !index || !netLow || !netHigh || batch.Count() != index.Count() || batch.Count() > MAX_IDS)
		{
			return false;
		}
		if (netLow.Count() != netHigh.Count())
		{
			return false;
		}
		return netLow.Count() == 0 || netLow.Count() == batch.Count();
	}
}
