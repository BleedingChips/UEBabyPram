module;
#include "Trace/DataStream.h"

export module UEBabyPramInsightParserInterface;

export namespace UEBabyPram::InsightParser
{

	struct DataResourceInterface : public UE::Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) = 0;
	};
}


//export import UEBabyPramInsightInterface;