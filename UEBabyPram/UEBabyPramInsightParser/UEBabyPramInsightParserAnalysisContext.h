#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Allocators.h"
#include "TraceServices/Model/AnalysisCache.h"
#include "Trace/Analysis.h"
#include "TraceServices/Containers/SlabAllocator.h"
#include "Common/StringStore.h"
#include "AnalysisCache.h"
#include "AnalysisServicePrivate.h"

#include "UEBabyPramInsightParserAnalysisInterface.h"

namespace UEBabyPram::InsightParser
{

	using TraceServices::IStringStore;
	using TraceServices::FSlabAllocator;
	using TraceServices::FStringStore;
	using TraceServices::ETimingProfilerTimerType;

	struct AnalysisContext;

	struct ContextThreadWrapper
		: public TraceServices::IEditableTimeline<TraceServices::FTimingProfilerEvent>
	{
		ContextThreadWrapper(uint32 InThreadId, AnalysisContext* Context)
			: ThreadId(InThreadId)
			, Context(Context)
		{

		}

		virtual void AppendBeginEvent(double StartTime, const TraceServices::FTimingProfilerEvent& Event) override;
		virtual void AppendEndEvent(double EndTime) override;

		// The ThreadId of this thread
		uint32 ThreadId;

		// The provider to forward calls to
		AnalysisContext* Context;
	};

	struct AnalysisContext
	{
		virtual const TCHAR* StoreString(const TCHAR* String) { return StringStore.Store(String); }
		virtual const TCHAR* StoreString(const FStringView& String) { return StringStore.Store(String); }
		virtual uint32 AddMetadata(uint32 MasterTimerId, TArray<uint8>&& Metadata) { return 0; }
		virtual TArrayView<uint8> GetEditableMetadata(uint32 TimerId) { return {}; }
		virtual void SetMetadata(uint32 MetadataTimerId, TArray<uint8>&& Metadata, uint32 NewTimerId) {}
		virtual void SetMetadataSpec(uint32 TimerId, uint32 MetadataSpecId) {}
		uint32 AddMetadataSpec(FMetadataSpec&& Metadata) { return 0; }
		virtual uint32 AddCpuTimer(FStringView Name, const TCHAR* File, uint32 Line) { return 0; }
		virtual void SetTimerLocation(uint32 TimerId, const TCHAR* File, uint32 Line) {}
		void SetTimerName(uint32 TimerId, FStringView Name) {}
		virtual const ITimingProfilerProvider* GetReadProvider() const { return nullptr; }
		IEditableTimeline<FTimingProfilerEvent>& GetCpuThreadEditableTimeline(uint32 ThreadId);
		void AddThread(uint32 Id, const TCHAR* Name, EThreadPriority Priority);
		AnalysisContext() : Allocator(32 << 20), StringStore(Allocator){}
	protected:
		FSlabAllocator Allocator;
		FStringStore StringStore;

		// The state at any moment of the threads
		TMap<uint32, TUniquePtr<ContextThreadWrapper>> Threads;
	};
}
