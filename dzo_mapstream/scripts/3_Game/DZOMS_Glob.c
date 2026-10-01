// Glob matching (DESIGN.md 5.4): '*' = any characters, '?' = exactly one.
// Both arguments must already be lower case. Iterative, one backtrack point, no recursion.
class DZOMS_Glob
{
	static bool Match(string pattern, string name)
	{
		int pl = pattern.Length();
		int sl = name.Length();
		int i = 0;
		int j = 0;
		int star = -1;
		int mark = 0;
		string pc;
		while (j < sl)
		{
			if (i < pl)
			{
				pc = pattern.Get(i);
				if (pc == "?" || pc == name.Get(j))
				{
					i++;
					j++;
					continue;
				}
				if (pc == "*")
				{
					star = i;
					mark = j;
					i++;
					continue;
				}
			}
			if (star < 0)
			{
				return false;
			}
			i = star + 1;
			mark++;
			j = mark;
		}
		while (i < pl && pattern.Get(i) == "*")
		{
			i++;
		}
		return i == pl;
	}
}
