#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"

// TraceAnalysis
#include "Trace/Analyzer.h"

// TraceServices
#include "Model/MonotonicTimeline.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "TraceServices/Model/Threads.h"
#include "UEBabyPramInsightParserInterface.h"
#include "UEBabyPramInsightParserAnalysisInterface.h"
#include "UEBabyPramInsightParserAnalysisContext.h"

namespace UEBabyPram::InsightParser
{

	class FMemoryAnalyzer
		: public UE::Trace::IAnalyzer
	{
	public:
		FMemoryAnalyzer(BaseParser& Parser, AnalysisContext& Context);
		~FMemoryAnalyzer();
		virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
		virtual void OnAnalysisEnd() override;
		virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

	private:
		enum : uint16
		{
			RouteId_TagsSpec,
			RouteId_TagSetSpec,
			RouteId_TrackerSpec,
			RouteId_TagValue,
		};

		AnalysisContext& AnaContext;
		BaseParser& Parser;
		uint64 Sample = 0;
	};
}
