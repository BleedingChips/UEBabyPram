module;

export module UEBabyPramInsightDefine;

import std;
import Potato;



export namespace UEBabyPram::InsightParser
{
	constexpr auto insight_log = Potato::TMP::TypeString(u8"InsightLog");

	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;

	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;

	enum ETransport : uint8
	{
		_Unused = 0,
		Raw = 1,
		Packet = 2,
		TidPacket = 3,
		TidPacketSync = 4,
		Active = TidPacketSync,
	};

	enum ETransportTid : uint32
	{
		Events = 0,			// used to describe events
		Internal = 1,			// events to make the trace stream function
		Importants = Internal,		// important/cached events
		Bias,						// [Bias,End] = threads. Note bias can't be..
		/* ... */					// ..changed as it breaks backwards compat :(
		End = 0x3ffe,		// two msbs are user for packet markers
		Sync = 0x3fff,		// see Writer_SendSync()
	};
}