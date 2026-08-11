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
#include "UEBabyPramInsightParserInterface.h"

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
		uint32 AddMetadataSpec(FMetadataSpec&& Metadata) {
			return Parser.AddMetaDataLayout(Metadata.Format, Metadata.FieldNames.GetData(), Metadata.FieldNames.Num());
		}
		
		uint32 AddCpuTimer(FStringView Name, const TCHAR* File, uint32 Line) { 
			FStringView FileView(File);
			return Parser.OnCPUEventDiscoverd(Name.GetData(), Name.Len(), FileView.GetData(), FileView.Len(), Line);
		}

		void SetTimerLocation(uint32 TimerId, const TCHAR* File, uint32 Line) 
		{
			FStringView FileView(File);
			return Parser.OverrideCPUEventLocation(TimerId, FileView.GetData(), FileView.Len());
		}

		void SetTimerName(uint32 TimerId, FStringView Name) {
			return Parser.OverrideCPUEventName(TimerId, Name.GetData(), Name.Len());
		}
		virtual const ITimingProfilerProvider* GetReadProvider() const { return nullptr; }
		ThreadTimeLineInterface* GetThreadTimeLine(uint32 ThreadId)
		{
			return Parser.GetThreadTimeLine(ThreadId);
		}
		void AddThread(uint32 Id, ANSICHAR const* Name, EThreadPriority Priority)
		{
			Parser.AddThread(Id, Name);
		}
		AnalysisContext(BaseParser& Parser) : Allocator(32 << 20), StringStore(Allocator), Parser(Parser){}
	protected:
		FSlabAllocator Allocator;
		FStringStore StringStore;
		BaseParser& Parser;

		// The state at any moment of the threads
		TMap<uint32, TUniquePtr<ContextThreadWrapper>> Threads;
	};
}
