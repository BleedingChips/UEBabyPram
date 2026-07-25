#pragma once

#include "Trace/DataStream.h"

namespace UEBabyPram::InsightParser
{

	struct DataResourceInterface : public UE::Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) = 0;
	};

	struct InsightReciver
	{
		virtual bool RequireEventName(wchar_t const* event_name, std::size_t event_name_len) { return true; }
		virtual bool RequireThread(wchar_t const* thread_name, std::size_t thread_name_len) { return true; }
	};

	void TestImp(DataResourceInterface& resource);
}


//export import UEBabyPramInsightInterface;