class CfgPatches
{
	class dzo_mapstream
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DF_Scripts"};
	};
};

class CfgMods
{
	class dzo_mapstream
	{
		dir = "dzo_mapstream";
		name = "dzo-mapstream";
		author = "";
		version = "1.0";
		type = "mod";
		dependencies[] = {"Game", "Mission"};

		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] = {"dzo_mapstream/scripts/3_Game"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"dzo_mapstream/scripts/5_Mission"};
			};
		};
	};
};
