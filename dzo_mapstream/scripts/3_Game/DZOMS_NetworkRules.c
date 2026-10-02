// Networked rules (DESIGN.md 5.3). The list is evaluated from the last rule to the first;
// the first rule that matches decides (= the last match wins). No match means local.
class DZOMS_Rule
{
	static const int TOKEN_NONE = 0;
	static const int TOKEN_DOORS = 1;
	static const int TOKEN_LADDERS = 2;
	static const int TOKEN_ACTIONS = 3;
	static const int TOKEN_FIXED_ITEMS = 4;

	string text;
	bool negated;
	int token;
	string pattern;
	bool matchPath;
}

class DZOMS_NetworkRules
{
	static const int LOCAL = 0;
	static const int NETWORKED = 1;
	static const int NEEDS_OBJECT = 2;

	// @ results depend only on the type: shared by all rule lists, keyed "<token>|<type>"
	protected static ref map<string, int> s_TokenCache;

	protected ref array<ref DZOMS_Rule> m_Rules;
	protected ref map<string, int> m_Decisions;
	protected ref map<string, string> m_DecisionRules;

	void DZOMS_NetworkRules()
	{
		m_Rules = new array<ref DZOMS_Rule>();
		m_Decisions = new map<string, int>();
		m_DecisionRules = new map<string, string>();
	}

	static void ClearTokenCache()
	{
		s_TokenCache = null;
	}

	static void Parse(array<string> texts, array<ref DZOMS_Rule> outRules, array<string> outErrors)
	{
		for (int i = 0; i < texts.Count(); i++)
		{
			string original = texts.Get(i);
			string s = original;
			s.TrimInPlace();
			bool negated = false;
			if (s.Length() > 0 && s.Get(0) == "!")
			{
				negated = true;
				s = s.Substring(1, s.Length() - 1);
				s.TrimInPlace();
			}
			if (s == "")
			{
				outErrors.Insert("networked rule '" + original + "' is empty");
				continue;
			}

			DZOMS_Rule r = new DZOMS_Rule();
			r.text = original;
			r.negated = negated;
			if (s.Get(0) == "@")
			{
				string tok = s;
				tok.ToLower();
				if (tok == "@doors")
				{
					r.token = DZOMS_Rule.TOKEN_DOORS;
				}
				else if (tok == "@ladders")
				{
					r.token = DZOMS_Rule.TOKEN_LADDERS;
				}
				else if (tok == "@actions")
				{
					r.token = DZOMS_Rule.TOKEN_ACTIONS;
				}
				else if (tok == "@fixeditems")
				{
					r.token = DZOMS_Rule.TOKEN_FIXED_ITEMS;
				}
				else
				{
					outErrors.Insert("networked rule '" + original + "': unknown token");
					continue;
				}
			}
			else if (s.Contains("/") || s.Contains("\\"))
			{
				r.matchPath = true;
				r.pattern = DZOMS_TypeName.Normalize(s);
			}
			else
			{
				r.pattern = s;
				r.pattern.ToLower();
			}
			outRules.Insert(r);
		}
	}

	// globalRules first, then mapRules (later rules win)
	static DZOMS_NetworkRules Combine(array<ref DZOMS_Rule> globalRules, array<ref DZOMS_Rule> mapRules)
	{
		DZOMS_NetworkRules nr = new DZOMS_NetworkRules();
		for (int i = 0; i < globalRules.Count(); i++)
		{
			nr.m_Rules.Insert(globalRules.Get(i));
		}
		for (int j = 0; j < mapRules.Count(); j++)
		{
			nr.m_Rules.Insert(mapRules.Get(j));
		}
		return nr;
	}

	int Decide(string type, out string rule)
	{
		return Evaluate(type, null, null, false, rule);
	}

	// obj may be NULL: @ rules then do not match
	int DecideWithObject(string type, Object obj, DZOMS_ObjectProbe probe, out string rule)
	{
		return Evaluate(type, obj, probe, true, rule);
	}

	protected int Evaluate(string type, Object obj, DZOMS_ObjectProbe probe, bool haveObject, out string rule)
	{
		string key = DZOMS_TypeName.Normalize(type);
		int cached;
		if (m_Decisions.Find(key, cached))
		{
			rule = m_DecisionRules.Get(key);
			return cached;
		}

		string name = DZOMS_TypeName.Of(type);
		string path = "";
		bool pathKnown = false;
		int result = LOCAL;
		rule = "";
		for (int i = m_Rules.Count() - 1; i >= 0; i--)
		{
			DZOMS_Rule r = m_Rules.Get(i);
			bool matches = false;
			if (r.token != DZOMS_Rule.TOKEN_NONE)
			{
				if (!haveObject)
				{
					rule = "";
					return NEEDS_OBJECT;
				}
				matches = TokenMatches(r.token, key, obj, probe);
			}
			else if (r.matchPath)
			{
				if (!pathKnown)
				{
					path = DZOMS_TypeName.ModelPath(type);
					pathKnown = true;
				}
				matches = DZOMS_Glob.Match(r.pattern, path);
			}
			else
			{
				matches = DZOMS_Glob.Match(r.pattern, name);
			}

			if (matches)
			{
				rule = r.text;
				result = NETWORKED;
				if (r.negated)
				{
					result = LOCAL;
				}
				break;
			}
		}

		if (!haveObject || obj)
		{
			m_Decisions.Set(key, result);
			m_DecisionRules.Set(key, rule);
		}
		return result;
	}

	protected static bool TokenMatches(int token, string key, Object obj, DZOMS_ObjectProbe probe)
	{
		if (!obj)
		{
			return false;
		}
		if (!s_TokenCache)
		{
			s_TokenCache = new map<string, int>();
		}
		string ck = token.ToString() + "|" + key;
		int cached;
		if (s_TokenCache.Find(ck, cached))
		{
			return cached == 1;
		}

		bool answer = false;
		if (token == DZOMS_Rule.TOKEN_DOORS)
		{
			answer = HasDoors(obj);
		}
		else if (token == DZOMS_Rule.TOKEN_LADDERS)
		{
			answer = HasLadder(obj);
		}
		else if (probe && token == DZOMS_Rule.TOKEN_ACTIONS)
		{
			answer = probe.HasOwnActions(obj);
		}
		else if (probe && token == DZOMS_Rule.TOKEN_FIXED_ITEMS)
		{
			answer = probe.IsFixedItem(obj);
		}

		int stored = 0;
		if (answer)
		{
			stored = 1;
		}
		s_TokenCache.Set(ck, stored);
		return answer;
	}

	static bool HasDoors(Object obj)
	{
		Building b = Building.Cast(obj);
		if (!b)
		{
			return false;
		}
		return b.GetDoorCount() > 0;
	}

	static bool HasLadder(Object obj)
	{
		if (!obj)
		{
			return false;
		}
		TStringArray selections = new TStringArray();
		obj.GetSelectionList(selections);
		for (int i = 0; i < selections.Count(); i++)
		{
			if (IsLadderSelection(selections.Get(i)))
			{
				return true;
			}
		}
		return false;
	}

	// "ladder" followed by at least one digit and nothing else, case-insensitive
	static bool IsLadderSelection(string name)
	{
		string s = name;
		s.ToLower();
		int len = s.Length();
		if (len < 7 || s.IndexOf("ladder") != 0)
		{
			return false;
		}
		for (int i = 6; i < len; i++)
		{
			int c = s.Get(i).ToAscii();
			if (c < 48 || c > 57)
			{
				return false;
			}
		}
		return true;
	}
}
