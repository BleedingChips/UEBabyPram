module;

export module UEBabyPramInsightFilterBookMarks;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Report the bookmark events which the query selects. The query accepts one chain element, which is matched
	// against the decoded text of the event, and the Time or ITime condition, which selects the events whose time
	// point is inside one of the slices; a query with no time condition selects every time. The spec of an event is
	// only read to decode its text, the report does not print the specs, the ids or the call stacks.
	struct BookMarksParser : public Parser
	{
		struct Config
		{
			// The query of the analysis, it is built by ParseEventQuery and checked by the command line, so the chain
			// carries exactly one element and the only condition is Time or ITime.
			EventQuery query;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One bookmark spec, the text of the file and of the format string is kept in UTF-8 because the report writes
		// UTF-8, and the wide format string is kept for the formatting of the events.
		struct BookMarkSpec
		{
			explicit BookMarkSpec(std::pmr::memory_resource* resource)
				: file_name(resource)
				, format_string(resource)
				, format_string_wide(resource) {}

			int64 id = 0;
			int32 line = 0;
			std::pmr::u8string file_name;
			std::pmr::u8string format_string;
			std::pmr::wstring format_string_wide;
		};

		// One bookmark event which the query selects: the time and the decoded text.
		struct BookMarkEvent
		{
			explicit BookMarkEvent(std::pmr::memory_resource* resource)
				: text(resource) {}

			DurationSec time = DurationSec::zero();
			std::pmr::u8string text;
		};

		BookMarksParser(Config config = {});

		// Store every bookmark spec which the trace declares, the text of the events is decoded by them.
		virtual void OnBookMarksSpec(int64 book_marks_id, int32 book_marks_line, std::u8string_view file_name, std::wstring_view format_string) override;

		// Store the bookmark event when the query selects its time and its decoded text.
		virtual void OnBookMarks(BookMarksId book_marks_id, DurationSec time, std::span<std::uint8_t const> data, std::uint32_t call_stack_id) override;

		// Both the specs and the events are read by the report.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;

		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The specs and the selected events which the trace declared.
		std::span<BookMarkSpec const> GetBookMarkSpecs() const { return specs; }
		std::span<BookMarkEvent const> GetBookMarkEvents() const { return events; }

	protected:

		// Whether the time point is selected by the time condition of the query. Both Time and ITime select the same
		// point, and a query with no slice selects every time.
		bool IsTimeSelected(DurationSec time) const;

		// Whether the decoded text is matched by the only chain element of the query. The element which matches any
		// text, ex: %, selects every event.
		bool IsTextSelected(std::u8string_view text) const;

		std::pmr::unsynchronized_pool_resource resource;
		std::pmr::vector<BookMarkSpec> specs;
		std::pmr::vector<BookMarkEvent> events;
		// The query which the events are selected by.
		EventQuery query;
		// The index of the spec of every id, so an event finds its format string without walking the specs.
		std::pmr::unordered_map<int64, std::size_t> spec_indices;
	};
}
