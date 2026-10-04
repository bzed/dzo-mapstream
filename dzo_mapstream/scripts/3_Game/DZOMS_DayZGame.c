modded class DayZGame
{
	protected ref DZOMS_ServerWorld m_DZOMS_ServerWorld;
	protected ref DZOMS_ClientWorld m_DZOMS_ClientWorld;

	void DZOMS_SetClientWorld(DZOMS_ClientWorld world)
	{
		m_DZOMS_ClientWorld = world;
	}

	DZOMS_ClientWorld DZOMS_GetClientWorld()
	{
		return m_DZOMS_ClientWorld;
	}

	void DZOMS_SetServerWorld(DZOMS_ServerWorld world)
	{
		m_DZOMS_ServerWorld = world;
	}

	DZOMS_ServerWorld DZOMS_GetServerWorld()
	{
		return m_DZOMS_ServerWorld;
	}

	override void OnRPC(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
	{
		if (target || rpc_type < DZOMS_Rpc.FIRST || rpc_type > DZOMS_Rpc.LAST)
		{
			super.OnRPC(sender, target, rpc_type, ctx);
			return;
		}

		// our ids never reach super
		if (m_DZOMS_ServerWorld && (rpc_type == DZOMS_Rpc.REQUEST_MANIFEST || rpc_type == DZOMS_Rpc.REQUEST_BATCH))
		{
			m_DZOMS_ServerWorld.OnRpc(sender, rpc_type, ctx);
		}
		else if (m_DZOMS_ClientWorld && (rpc_type == DZOMS_Rpc.MANIFEST || rpc_type == DZOMS_Rpc.BATCH || rpc_type == DZOMS_Rpc.OBJECTS_NETWORKED || rpc_type == DZOMS_Rpc.OBJECTS_LOCAL))
		{
			m_DZOMS_ClientWorld.OnRpc(rpc_type, ctx);
		}
	}
}
