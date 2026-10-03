// FindFile path helper for DayZ 1.29 and 1.30 (copy of the dayz-dev skill fallback, no CF dependency).
// 1.30 ignores $profile:/$mission:/$storage: in FindFile (DZEXP-134) and silently lists the server root,
// so every FindFile pattern goes through DZOMS_FindFilePath. All other file functions accept the placeholders.
// Needs -profiles=<dir> on 1.30.

//! "mpmissions/dayzOffline.chernarusplus/mission.c" -> "mpmissions/dayzOffline.chernarusplus".
//! Not g_Game.GetMissionFolderPath(): it only splits on '\' and returns "" on both 1.29 and 1.30 servers.
static string DZOMS_MissionFolder()
{
	string path = g_Game.GetMissionPath();
	path.Replace("\\", "/");
	int idx = path.LastIndexOf("/");
	if (idx < 0)
		return "";
	return path.Substring(0, idx);
}

static string DZOMS_FindFilePath(string pattern)
{
	pattern.Replace("\\", "/");

	string lower = pattern;
	lower.ToLower();

	string root;
	int cut = -1;
	if (lower.IndexOf("$storage:") == 0)
	{
		cut = 9;
		//! -storage=<dir> moves the storage_<instanceId> folder out of the mission folder
		if (!GetCLIParam("storage", root))
			root = DZOMS_MissionFolder();
		if (root != "")
			root = string.Format("%1/storage_%2", root, g_Game.ServerConfigGetInt("instanceId"));
	}
#ifndef DAYZ_1_29
	else if (lower.IndexOf("$profile:") == 0)
	{
		cut = 9;
		GetCLIParam("profiles", root);
	}
	else if (lower.IndexOf("$mission:") == 0)
	{
		cut = 9;
		root = DZOMS_MissionFolder();
	}
	else if (lower.IndexOf("$currentdir:") == 0)
	{
		return pattern.Substring(12, pattern.Length() - 12);
	}
#endif

	if (cut < 0)
		return pattern;

	if (root == "")
	{
		//! Not ErrorEx/Error: on diag builds (every experimental server) those become VM exceptions
		Print("[dzo-mapstream] WARNING: cannot resolve " + pattern + " for FindFile (start the server with -profiles=<dir>)");
		return pattern;
	}

	root.Replace("\\", "/");
	if (root.Length() > 0 && root.Get(root.Length() - 1) == "/")
		root = root.Substring(0, root.Length() - 1);

	return root + "/" + pattern.Substring(cut, pattern.Length() - cut);
}
