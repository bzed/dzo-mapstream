// Checks that need classes of the 4_World module (BuildingBase, ItemBase).
// 4_World provides a subclass (DZOMS_ObjectProbeWorld); this base answers "no" to everything.
class DZOMS_ObjectProbe : Managed
{
	bool HasOwnActions(Object obj)
	{
		return false;
	}

	bool IsFixedItem(Object obj)
	{
		return false;
	}
}
