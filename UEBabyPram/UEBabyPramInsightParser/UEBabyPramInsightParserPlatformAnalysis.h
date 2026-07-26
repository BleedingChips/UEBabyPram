#pragma once

#include "CoreTypes.h"
#include "Trace/Analyzer.h"
#include "ProfilingDebugging/MiscTrace.h"

#include "UEBabyPramInsightParserAnalysisInterface.h"
#include "UEBabyPramInsightParserAnalysisSession.h"

namespace UEBabyPram::InsightParser
{

	class FPlatformEventTraceAnalyzer
		: public UE::Trace::IAnalyzer
	{
	public:
		FPlatformEventTraceAnalyzer(IAnalysisSession& Session);
		virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
		virtual void OnAnalysisEnd() override;
		virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
		virtual void OnThreadInfo(const FThreadInfo& ThreadInfo) override;

	private:
		enum : uint16
		{
			RouteId_Settings,
			RouteId_ContextSwitch,
			RouteId_StackSample,
			RouteId_ThreadName,
		};

		IAnalysisSession& Session;
	};

} // namespace TraceServices
