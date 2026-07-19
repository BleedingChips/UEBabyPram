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

	struct CpuReceiverInterface
	{
		virtual bool RequireThread(std::u8string_view thread_name) { return true; }
		//virtual void OnCPUEventCreated();
	};
}


//export import UEBabyPramInsightInterface;