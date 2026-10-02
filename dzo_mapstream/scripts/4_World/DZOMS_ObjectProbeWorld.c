class DZOMS_ObjectProbeWorld : DZOMS_ObjectProbe
{
	override bool HasOwnActions(Object obj)
	{
		BuildingBase b = BuildingBase.Cast(obj);
		if (!b)
		{
			return false;
		}
		return b.DZOMS_CountOwnActions() > 0;
	}

	// items players cannot pick up (placed targets, territory flags). Never CanPutInCargo(NULL): not null-safe.
	override bool IsFixedItem(Object obj)
	{
		ItemBase item = ItemBase.Cast(obj);
		if (!item)
		{
			return false;
		}
		return !item.CanPutIntoHands(null);
	}
}
