class DZOMS_TestFixtures
{
	// sets the fields directly: EditorObjectData.Create would spawn an object
	static void AddObject(EditorSaveData data, string type, vector pos, vector ori, float scale, int flags)
	{
		EditorObjectData d = new EditorObjectData();
		d.Type = type;
		d.Position = pos;
		d.Orientation = ori;
		d.Scale = scale;
		d.Flags = flags;
		// as the Editor stores it: the property is separate from the flag bit
		d.EditorOnly = (flags & EditorObjectFlags.EDITOR_ONLY) == EditorObjectFlags.EDITOR_ONLY && flags != 158;
		data.EditorObjects.Insert(d);
	}

	static void AddHidden(EditorSaveData data, Object obj)
	{
		EditorDeletedObjectData d = EditorDeletedObjectData.Create(obj);
		if (d)
		{
			data.EditorHiddenObjects.Insert(d);
		}
	}

	static bool Save(EditorSaveData data, string path)
	{
		FileSerializer ser = new FileSerializer();
		if (!ser.Open(path, FileMode.WRITE))
		{
			return false;
		}
		data.Write(ser, EditorSaveData.Version);
		ser.Close();
		return true;
	}

	// a suppressible map object: a Land_ house if there is one nearby, else a model-only object
	// (empty type). Map objects do have network ids, so that is not a criterion.
	static Object FindWorldBuilding()
	{
		array<vector> centers = {"10340 0 2300", "6600 0 2500", "4500 0 10300", "12000 0 9200", "7500 0 5200"};
		Object fallback = null;
		for (int i = 0; i < centers.Count(); i++)
		{
			vector c = centers.Get(i);
			c[1] = g_Game.SurfaceY(c[0], c[2]);
			array<Object> objects = new array<Object>();
			g_Game.GetObjectsAtPosition3D(c, 300, objects, null);
			for (int j = 0; j < objects.Count(); j++)
			{
				Object o = objects.Get(j);
				if (!o || o.GetShapeName() == "")
				{
					continue;
				}
				if (GetDayZGame().GetSuppressedObjectManager().IsSuppressed(o))
				{
					continue;
				}
				string type = o.GetType();
				if (o.IsKindOf("House") && type.IndexOf("Land_") == 0)
				{
					return o;
				}
				if (!fallback && type == "" && o.GetShapeName().Contains("structures"))
				{
					fallback = o;
				}
			}
		}
		return fallback;
	}
}
