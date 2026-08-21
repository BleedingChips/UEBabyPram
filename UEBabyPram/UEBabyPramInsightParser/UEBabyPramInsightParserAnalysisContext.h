#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Containers/Allocators.h"
#include "TraceServices/Model/AnalysisCache.h"
#include "Trace/Analysis.h"
#include "TraceServices/Containers/SlabAllocator.h"
#include "Common/StringStore.h"
#include "AnalysisCache.h"
#include "AnalysisServicePrivate.h"
#include "Trace/Analyzer.h"

#include "UEBabyPramInsightParserAnalysisInterface.h"
#include "UEBabyPramInsightParserInterface.h"


namespace UEBabyPram::InsightParser
{

	using TraceServices::IStringStore;
	using TraceServices::FSlabAllocator;
	using TraceServices::FStringStore;
	using TraceServices::ETimingProfilerTimerType;

	struct AnalysisContext;

	struct AnalysisContext
	{
		virtual const TCHAR* StoreString(const TCHAR* String) { return StringStore.Store(String); }
		virtual const TCHAR* StoreString(const FStringView& String) { return StringStore.Store(String); }

		AnalysisContext() : Allocator(32 << 20), StringStore(Allocator) {}
	protected:
		FSlabAllocator Allocator;
		FStringStore StringStore;
	};
}
