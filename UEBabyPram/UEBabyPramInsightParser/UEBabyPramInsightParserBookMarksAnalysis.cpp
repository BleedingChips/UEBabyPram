
#include "UEBabyPramInsightParserBookMarksAnalysis.h"

#include "HAL/LowLevelMemTracker.h"
#include "Model/ContextSwitchesPrivate.h"
#include "Model/StackSamplesPrivate.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace UEBabyPram::InsightParser
{

	FBookmarksAnalyzer::FBookmarksAnalyzer(BaseParser& Parser)
		: Parser(Parser)
	{
	}

	void FBookmarksAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
	{
		auto& Builder = Context.InterfaceBuilder;

		if (Parser.IsParserRequired(ParserRequireFlag::BOOK_MARKS_SPEC))
		{
			Builder.RouteEvent(RouteId_BookmarkSpec, "Misc", "BookmarkSpec");

		}

		if (Parser.IsParserRequired(ParserRequireFlag::BOOK_MARKS))
		{
			Builder.RouteEvent(RouteId_Bookmark, "Misc", "Bookmark");
		}

	}

	bool FBookmarksAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
	{

		const auto& EventData = Context.EventData;
		switch (RouteId)
		{
		case RouteId_BookmarkSpec:
		{
			uint64 BookmarkPoint = EventData.GetValue<uint64>("BookmarkPoint");
			const TCHAR* BookmarkFormatString = nullptr;
			const TCHAR* BookmarkFile = nullptr;
			int32 BookmarkLine = EventData.GetValue<int32>("Line");

			FAnsiStringView file_name;
			EventData.GetString("FileName", file_name);

			FWideStringView format_string_w;

			if (file_name.Len() > 0)
			{
				EventData.GetString("FormatString", format_string_w);
			}
			else {
				auto file_name_str = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
				file_name = file_name_str;
				format_string_w = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(file_name_str) + 1);
			}

			Parser.OnBookMarksSpec(
				static_cast<int64>(BookmarkPoint),
				BookmarkLine,
				file_name.GetData(),
				file_name.Len(),
				format_string_w.GetData(),
				format_string_w.Len()
			);

			break;
		}

		case RouteId_Bookmark:
		{
			uint64 BookmarkPoint = EventData.GetValue<uint64>("BookmarkPoint");
			uint64 Cycle = EventData.GetValue<uint64>("Cycle");
			double Timestamp = Context.EventTime.AsSeconds(Cycle);
			TArrayView<const uint8> Out = Context.EventData.GetArrayView<uint8>("FormatArgs");
			if (Out.GetData() == nullptr)
			{
				Out = TArrayView<const uint8>(
					Context.EventData.GetAttachment(),
					Context.EventData.GetAttachmentSize());
			}
			//TArrayView<const uint8> FormatArgsView = FTraceAnalyzerUtils::LegacyAttachmentArray("FormatArgs", Context);
			uint32 CallstackId = EventData.GetValue<uint32>("CallstackId");
			Parser.OnBookMarks(BookmarkPoint, Timestamp, Out.GetData(), Out.Num(), CallstackId);
			{
				// Todo: update this to use provider locking instead of session locking
				// FProviderEditScopeLock EditableBookmarkProviderLock (EditableBookmarkProvider);
				//EditableBookmarkProvider.AppendBookmark(BookmarkPoint, Timestamp, CallstackId, FormatArgsView.GetData());
			}

			//if (EditableLogProvider)
			{
				// Todo: update this to use provider locking instead of session locking
				// FProviderEditScopeLock EditableLogProviderLock (*EditableLogProvider);
				//EditableLogProvider->AppendMessage(BookmarkPoint, Timestamp, FormatArgsView.GetData());
			}
			break;
		}
		}

		return true;
	}

} // namespace TraceServices


//export import UEBabyPramInsightInterface;