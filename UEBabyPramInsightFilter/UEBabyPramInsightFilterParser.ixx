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