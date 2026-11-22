module;

#include <re2/re2.h>
#include "Common/FormatArgs.h"

module UEBabyPramInsightFilterBookMarks;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// Format the arguments of one bookmark event by the format string of its spec. The helper of the trace services
		// is the one which the engine uses for the same job, so a bookmark which carries a string, an integer or a
		// floating point argument is reported the same way as the engine reports it.
		std::u8string FormatBookMarkText(std::wstring_view format_string, std::span<std::uint8_t const> data)
		{
			if (format_string.empty())
			{
				return {};
			}

			constexpr std::size_t buffer_size = 65536;
			std::vector<wchar_t> text_buffer(buffer_size, L'\0');
			std::vector<wchar_t> temp_buffer(buffer_size, L'\0');

			std::uint8_t empty_data = 0;
			std::uint8_t const* data_ptr = data.empty() ? &empty_data : data.data();

			TraceServices::FFormatArgsHelper::Format(
				text_buffer.data(),
				buffer_size - 1,
				temp_buffer.data(),
				buffer_size - 1,
				std::wstring{ format_string }.c_str(),
				data_ptr
			);

			std::u8string text;
			Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(
				std::wstring_view{ text_buffer.data() },
				std::back_inserter(text)
			);

			return text;
		}

		std::wstring ToWide(std::u8string_view text)
		{
			std::wstring wide;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(text, std::back_inserter(wide));
			return wide;
		}
	}

	BookMarksParser::BookMarksParser(Config in_config)
		: resource(in_config.resource)
		, specs(&resource)
		, events(&resource)
		, query(std::move(in_config.query))
		, spec_indices(&resource)
	{
	}

	bool BookMarksParser::IsTimeSelected(DurationSec time) const
	{
		// Time and ITime are the same for a bookmark, because a bookmark is a point in the time line: the point is
		// selected when one of the slices contains it or is equal to it. A query with no slice selects every time.
		if (query.time_ranges.empty())
		{
			return true;
		}

		for (auto const& range : query.time_ranges)
		{
			if (time >= range.Begin() && time <= range.End())
			{
				return true;
			}
		}

		return false;
	}

	bool BookMarksParser::IsTextSelected(std::u8string_view text) const
	{
		// The command line guarantees one element, but a parser which is built without it still selects every event
		// instead of dropping all of them.
		if (query.chain.empty() || query.chain.front().matcher == nullptr)
		{
			return true;
		}

		std::string_view utf8_text{
			reinterpret_cast<char const*>(text.data()),
			text.size()
		};

		return re2::RE2::FullMatch(utf8_text, *query.chain.front().matcher);
	}

	void BookMarksParser::OnBookMarksSpec(int64 book_marks_id, int32 book_marks_line, std::u8string_view file_name, std::wstring_view format_string)
	{
		BookMarkSpec spec{ &resource };
		spec.id = book_marks_id;
		spec.line = book_marks_line;
		spec.file_name.assign(file_name.begin(), file_name.end());
		spec.format_string_wide.assign(format_string.begin(), format_string.end());

		// The format string of the trace is wide, the report is UTF-8, so it is converted once when the spec arrives
		// instead of once for every line of the report.
		Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(format_string, std::back_inserter(spec.format_string));

		spec_indices.try_emplace(book_marks_id, specs.size());
		specs.push_back(std::move(spec));
	}

	void BookMarksParser::OnBookMarks(BookMarksId book_marks_id, DurationSec time, std::span<std::uint8_t const> data, std::uint32_t)
	{
		// The id and the call stack are only read to decode the text, the report does not print them.
		auto const found = spec_indices.find(static_cast<int64>(book_marks_id.id));
		std::u8string text = found != spec_indices.end()
			? FormatBookMarkText(specs[found->second].format_string_wide, data)
			: std::u8string{ u8"<No Spec>" };

		if (!IsTimeSelected(time) || !IsTextSelected(text))
		{
			return;
		}

		BookMarkEvent event{ &resource };
		event.time = time;
		event.text = std::move(text);
		events.push_back(std::move(event));
	}

	bool BookMarksParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// Both the specs and the events are read, so the analyzer registers the routes of both of them.
		return flag == ParserRequireFlag::BOOK_MARKS_SPEC
			|| flag == ParserRequireFlag::BOOK_MARKS;
	}

	void BookMarksParser::AllAnalyzeDone()
	{
		// The events of the trace are in the order of the time, but a trace which carries several streams can deliver
		// them slightly out of order, so the report sorts them before it prints them.
		std::stable_sort(events.begin(), events.end(), [](BookMarkEvent const& left, BookMarkEvent const& right) {
			return left.time < right.time;
		});

		Parser::AllAnalyzeDone();
	}

	bool BookMarksParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		PrintReportLine(printer, L"BookMarksParser Output:");
		PrintReportLine(printer, std::format(L"    Bookmark Event <{}>", events.size()));

		if (events.empty())
		{
			PrintReportLine(printer, L"    No bookmark event is selected by the query.");
			return true;
		}

		// The specs are only the source of the format string, so the report prints the events only and leaves the ids
		// and the call stacks out of every line.
		for (std::size_t index = 0; index < events.size(); ++index)
		{
			BookMarkEvent const& event = events[index];

			PrintReportLine(printer, std::format(
				L"    [{}] Time:<{}>, Text:<{}>",
				index,
				FormatTimePoint(event.time),
				ToWide(event.text)
			));
		}

		return true;
	}
}
