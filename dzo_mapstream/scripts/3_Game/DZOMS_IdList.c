// Ids of objects as (batch, index inside the batch), for the networked/local announcements
// (DESIGN 13.5). Flat arrays only, like the other payload classes.
class DZOMS_IdList
{
	const int MAX_IDS = 500;

	ref array<int> batch;
	ref array<int> index;

	void DZOMS_IdList()
	{
		batch = new array<int>();
		index = new array<int>();
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

	// equal lengths and not more than MAX_IDS: what a receiver may use
	bool IsValid()
	{
		return batch && index && batch.Count() == index.Count() && batch.Count() <= MAX_IDS;
	}
}
