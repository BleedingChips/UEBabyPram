module;
#include "Trace/DataStream.h"

export module UEBabyPramInsightParserInterface;
import std;

export namespace UEBabyPram::InsightParser
{

	struct DataResourceInterface : public UE::Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) = 0;
	};

	struct InsightReciver
	{
		virtual bool RequireEventName(std::wstring_view event_name) { return true; }
		virtual bool RequireThread(std::u8string_view thread_name) { return true; }
	};
}


//export import UEBabyPramInsightInterface;