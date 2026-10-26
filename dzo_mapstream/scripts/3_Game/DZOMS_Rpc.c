class DZOMS_Rpc
{
	// 0x445A4D01 .. 0x445A4D06
	const int REQUEST_MANIFEST = 1146768641;
	const int MANIFEST = 1146768642;
	const int REQUEST_BATCH = 1146768643;
	const int BATCH = 1146768644;
	// server -> all clients: objects that are networked now / local again (DESIGN 13.5)
	const int OBJECTS_NETWORKED = 1146768645;
	const int OBJECTS_LOCAL = 1146768646;
	const int FIRST = 1146768641;
	const int LAST = 1146768646;

	const int PROTOCOL_VERSION = 3;
	const int CACHE_VERSION = 1;
}
