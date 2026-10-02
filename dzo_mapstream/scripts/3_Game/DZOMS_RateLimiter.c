// Per identity limits (DESIGN.md 6.4): 1 manifest per second, batches by a token bucket
// (capacity 20, refill 20 per second). Tokens are kept in thousandths so the arithmetic is exact.
class DZOMS_RateLimiter
{
	static const int BUCKET_CAPACITY = 20000;
	static const int REFILL_PER_MS = 20;	// 0.02 tokens per ms
	static const int TOKEN_COST = 1000;
	static const int MANIFEST_INTERVAL_MS = 1000;
	static const int IDLE_MS = 120000;

	protected ref map<string, int> m_LastManifest = new map<string, int>();
	protected ref map<string, int> m_Tokens = new map<string, int>();
	protected ref map<string, int> m_LastRefill = new map<string, int>();
	protected ref map<string, int> m_LastSeen = new map<string, int>();

	bool AllowManifest(string id, int nowMs)
	{
		m_LastSeen.Set(id, nowMs);
		int last;
		if (m_LastManifest.Find(id, last) && (nowMs - last) < MANIFEST_INTERVAL_MS)
		{
			return false;
		}
		m_LastManifest.Set(id, nowMs);
		return true;
	}

	bool AllowBatch(string id, int nowMs)
	{
		m_LastSeen.Set(id, nowMs);
		int tokens = BUCKET_CAPACITY;
		int lastRefill;
		if (m_Tokens.Find(id, tokens) && m_LastRefill.Find(id, lastRefill))
		{
			int elapsed = nowMs - lastRefill;
			if (elapsed > 0)
			{
				if (elapsed > BUCKET_CAPACITY)
				{
					elapsed = BUCKET_CAPACITY;	// anything above this fills the bucket anyway
				}
				tokens = Math.Min(BUCKET_CAPACITY, tokens + elapsed * REFILL_PER_MS);
			}
		}
		else
		{
			tokens = BUCKET_CAPACITY;
		}
		m_LastRefill.Set(id, nowMs);

		if (tokens < TOKEN_COST)
		{
			m_Tokens.Set(id, tokens);
			return false;
		}
		m_Tokens.Set(id, tokens - TOKEN_COST);
		return true;
	}

	void Remove(string id)
	{
		m_LastManifest.Remove(id);
		m_Tokens.Remove(id);
		m_LastRefill.Remove(id);
		m_LastSeen.Remove(id);
	}

	// drops identities not seen for more than 120 s; keys are collected first, never removed while iterating
	void Sweep(int nowMs)
	{
		array<string> stale = new array<string>();
		for (int i = 0; i < m_LastSeen.Count(); i++)
		{
			if ((nowMs - m_LastSeen.GetElement(i)) > IDLE_MS)
			{
				stale.Insert(m_LastSeen.GetKey(i));
			}
		}
		for (int j = 0; j < stale.Count(); j++)
		{
			Remove(stale.Get(j));
		}
	}

	int Count()
	{
		return m_LastSeen.Count();
	}
}
