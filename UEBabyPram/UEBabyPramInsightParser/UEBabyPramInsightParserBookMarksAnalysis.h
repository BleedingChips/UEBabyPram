#pragma once

#include "CoreTypes.h"
#include "Trace/Analyzer.h"
#include "ProfilingDebugging/MiscTrace.h"

#include "UEBabyPramInsightParserInterface.h"
#include "UEBabyPramInsightParserAnalysisInterface.h"


namespace UEBabyPram::InsightParser
{
	class FBookmarksAnalyzer
		: public UE::Trace::IAnalyzer
	{
	public:
		FBookmarksAnalyzer(BaseParser& Parser);
		virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
		virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

	private:
		enum : uint16
		{
			RouteId_BookmarkSpec,
			RouteId_Bookmark,
		};

		BaseParser& Parser;
		uint64 BookmarkLogCategoryId = uint64(-1);

		enum
		{
			FormatBufferSize = 65536
		};
		TCHAR FormatBuffer[FormatBufferSize];
		TCHAR TempBuffer[FormatBufferSize];
	};

} // namespace TraceServices
