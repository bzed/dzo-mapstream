// Puts an object at a position with an orientation and a scale in one step. SetOrientation and
// SetYawPitchRoll reset the scale to 1; SetTransform with a scaled matrix keeps all three
// (verified on 1.29 with ray casts: scale 2 keeps top and footprint doubled, yaw as requested).
class DZOMS_Placement
{
	static void Apply(Object obj, vector pos, vector ori, float scale)
	{
		vector rot[3];
		Math3D.YawPitchRollMatrix(ori, rot);
		vector mat[4];
		for (int i = 0; i < 3; i++)
		{
			mat[i] = rot[i] * scale;
		}
		mat[3] = pos;
		obj.SetTransform(mat);
	}
}
