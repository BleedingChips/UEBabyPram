module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterParser;

import UEBabyPramInsightParser;
import Potato;
import std;

export namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
	
	struct Parser : public InsightParser::ParserInterface
	{
		virtual ~Parser() = default;
		virtual bool PrintToLog(Potato::Log::LogPrinter& printer) { return false; }
	};

	constexpr auto LogCategory = Potato::TMP::TypeString("FilterLog");
	constexpr auto OutputCategory = Potato::TMP::TypeString("UEBabyPram::InsightFilter");

	template<Potato::TMP::TypeString formatter, typename ...Parameters>
	constexpr void OutputLog(Potato::Log::LogPrinter& printer, Parameters&& ...parameters)
	{
		Potato::Log::LogTo<formatter>(
			printer,
			std::forward<Parameters>(parameters)...
		);
	}

	// Report that the trace carries no Context Switch event, which every parser which reads the context switch
	// streamer tells its report: the Context Switch Time of every event of such a trace is 0, and the Exclude Time
	// WithOut Context Switch Time is the same as the Exclude Time, so the time which a thread spent out of the CPU is
	// missing from the trace instead of being 0, and the time of the report is the time which the thread was scheduled.
	inline void OutputNoContextSwitchEventNote(Potato::Log::LogPrinter& printer)
	{
		OutputLog<L"    (No Context Switch event is recorded in the trace: the Context Switch Time (CS) of every event is 0 and the Exclude Time WithOut Context Switch Time (ENCS) is the same as the Exclude Time (E), the trace is captured without the context switch channel of the CPU)">(
			printer
		);
	}

	// Append one field of a CSV file: a field which carries the delimiter, the quote or a line break is quoted, and
	// the quote inside it is doubled, which is what RFC 4180 requires, so an event name which carries a comma stays
	// one field.
	inline void AppendCSVField(std::u8string& line, std::u8string_view field)
	{
		bool is_quoted =
			field.find(u8',') != std::u8string_view::npos
			|| field.find(u8'"') != std::u8string_view::npos
			|| field.find(u8'\n') != std::u8string_view::npos
			|| field.find(u8'\r') != std::u8string_view::npos;

		if (!is_quoted)
		{
			line.append(field);
			return;
		}

		line.push_back(u8'"');
		for (char8_t character : field)
		{
			if (character == u8'"')
			{
				line.push_back(u8'"');
			}
			line.push_back(character);
		}
		line.push_back(u8'"');
	}

	// Write one row of a CSV file: the fields are separated by the delimiter of the file, and the row is closed by its
	// line break, so the caller only appends the fields in the column order of the row.
	struct CSVRowWriter
	{
		explicit CSVRowWriter(std::u8string& output)
			: output(output) {}

		// Append a text field, the field is quoted when it has to be.
		void Text(std::u8string_view field)
		{
			Separate();
			AppendCSVField(output, field);
		}

		// Append a count or an index field.
		void Integer(std::size_t value)
		{
			std::string text = std::format("{}", value);
			Text(std::u8string_view{ reinterpret_cast<char8_t const*>(text.data()), text.size() });
		}

		// Append a time field in milliseconds, the time is written with 6 decimals, so a duration of one nanosecond
		// keeps its precision.
		void Milliseconds(DurationSec value)
		{
			std::string text = std::format("{:.6f}", value.count() * 1000.0);
			Text(std::u8string_view{ reinterpret_cast<char8_t const*>(text.data()), text.size() });
		}

		// Append a time field in seconds, the time is written with 6 decimals, which is the precision of the time range
		// of a frame.
		void Seconds(DurationSec value)
		{
			std::string text = std::format("{:.6f}", value.count());
			Text(std::u8string_view{ reinterpret_cast<char8_t const*>(text.data()), text.size() });
		}

		// Append a frame rate field, it is written with 2 decimals like it is reported.
		void Fps(double value)
		{
			std::string text = std::format("{:.2f}", value);
			Text(std::u8string_view{ reinterpret_cast<char8_t const*>(text.data()), text.size() });
		}

		// Close the row, the next row starts with the same writer.
		void EndLine()
		{
			output.push_back(u8'\n');
			has_field = false;
		}

	protected:

		void Separate()
		{
			if (has_field)
			{
				output.push_back(u8',');
			}

			has_field = true;
		}

		std::u8string& output;
		bool has_field = false;
	};

	// Check whether the event name is fully matched by the matcher, a partial match is not accepted.
	// The matcher needs the event name in UTF-8, the utf8_buffer is a scratch storage of the caller
	// which avoids an allocation per call.
	inline bool IsEventNameMatched(re2::RE2 const& matcher, std::wstring_view event_name, std::u8string& utf8_buffer)
	{
		utf8_buffer.clear();
		Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(
			event_name,
			std::back_inserter(utf8_buffer)
		);

		return re2::RE2::FullMatch(
			std::string_view{
				reinterpret_cast<char const*>(utf8_buffer.data()),
				utf8_buffer.size()
			},
			matcher
		);
	}

	// Check whether the event name is fully matched by the matcher, a partial match is not accepted.
	// The event name is already UTF-8, so it is matched in place without a copy and no scratch
	// storage is needed.
	inline bool IsEventNameMatched(re2::RE2 const& matcher, std::u8string_view event_name)
	{
		std::string_view name_view;
		if (!event_name.empty())
		{
			name_view = std::string_view{
				reinterpret_cast<char const*>(event_name.data()),
				event_name.size()
			};
		}

		return re2::RE2::FullMatch(name_view, matcher);
	}
}