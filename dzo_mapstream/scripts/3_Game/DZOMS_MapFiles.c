// Finds the .dze files in the DZE folder (DESIGN.md 5).
class DZOMS_MapFiles
{
	static const string EXTENSION = ".dze";

	// Plain file names of every *.dze in dir (any case), sorted case-insensitively so the load
	// order, and with it the batch hashes, does not depend on the file system.
	static void List(string dir, out array<string> names)
	{
		names = new array<string>();
		string fileName;
		FileAttr attr;
		// pattern "*" and a manual extension check: the pattern match is case sensitive on Linux
		FindFileHandle h = FindFile(dir + "/*", fileName, attr, FindFileFlags.ALL);
		bool found = fileName != "";
		while (found)
		{
			if ((attr & FileAttr.DIRECTORY) == 0 && IsMapFile(fileName))
			{
				names.Insert(fileName);
			}
			found = FindNextFile(h, fileName, attr);
		}
		if (h)
		{
			CloseFindFile(h);
		}
		SortNoCase(names);
	}

	static bool IsMapFile(string fileName)
	{
		string lower = fileName;
		lower.ToLower();
		int ext = EXTENSION.Length();
		return lower.Length() > ext && lower.Substring(lower.Length() - ext, ext) == EXTENSION;
	}

	// insertion sort: a handful of files, and Enforce's array.Sort is case sensitive
	protected static void SortNoCase(array<string> names)
	{
		for (int i = 1; i < names.Count(); i++)
		{
			string cur = names.Get(i);
			string curKey = cur;
			curKey.ToLower();
			int j = i - 1;
			while (j >= 0)
			{
				string other = names.Get(j);
				string otherKey = other;
				otherKey.ToLower();
				if (otherKey <= curKey)
				{
					break;
				}
				names.Set(j + 1, other);
				j--;
			}
			names.Set(j + 1, cur);
		}
	}
}
