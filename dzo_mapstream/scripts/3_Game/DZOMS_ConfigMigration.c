// Brings an older config.json up to date (DESIGN 5.6). Overridable so tests can use their own
// steps and make the backup fail.
class DZOMS_ConfigMigration
{
	int CurrentVersion()
	{
		return DZOMS_Config.CURRENT_VERSION;
	}

	// Applies the step to version toVersion to the loaded, not yet normalized config. False for a
	// step this mod does not know. what: log text of the change. No steps exist yet.
	bool Step(DZOMS_Config c, int toVersion, out string what)
	{
		return false;
	}

	// Copy of the admin's file; an existing backup of that name is replaced.
	bool Backup(string path, string backupPath)
	{
		DeleteFile(backupPath);
		return CopyFile(path, backupPath);
	}
}
