modded class BuildingBase
{
	int m_DZOMS_ActionCount;
	bool m_DZOMS_CountingActions;

	// Number of actions SetActions() registers. Calls SetActions() directly: InitializeActions()
	// only runs it for the first instance of a type, so it cannot be used to probe a type twice.
	int DZOMS_CountOwnActions()
	{
		m_DZOMS_ActionCount = 0;
		m_DZOMS_CountingActions = true;
		SetActions();
		m_DZOMS_CountingActions = false;
		return m_DZOMS_ActionCount;
	}

	override void AddAction(typename actionName)
	{
		if (m_DZOMS_CountingActions)
		{
			m_DZOMS_ActionCount++;
			return;
		}
		super.AddAction(actionName);
	}

	override void RemoveAction(typename actionName)
	{
		if (m_DZOMS_CountingActions)
		{
			return;
		}
		super.RemoveAction(actionName);
	}

	// a networked twin of a map object streams in with a default yaw and scale 1: the client world puts it at its entry
	override void EEInit()
	{
		super.EEInit();
		if (g_Game.IsDedicatedServer())
		{
			return;
		}
		DZOMS_ClientWorld world = GetDayZGame().DZOMS_GetClientWorld();
		if (world)
		{
			world.OnObjectInit(this);
		}
	}
}
