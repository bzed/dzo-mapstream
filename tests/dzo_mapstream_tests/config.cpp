class CfgPatches
{
	class dzo_mapstream_tests
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DF_Scripts", "dzo_mapstream"};
	};
};

class CfgMods
{
	class dzo_mapstream_tests
	{
		dir = "dzo_mapstream_tests";
		name = "dzo-mapstream tests";
		author = "";
		version = "1.0";
		type = "mod";
		dependencies[] = {"Game", "World", "Mission"};

		class defs
		{
			class missionScriptModule
			{
				value = "";
				files[] = {"dzo_mapstream_tests/scripts/5_Mission"};
			};
		};
	};
};
