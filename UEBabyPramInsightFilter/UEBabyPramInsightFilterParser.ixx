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
		Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display, formatter>(
			printer,
			std::forward<Parameters>(parameters)...
		);
	}

	// Describe the error of the matcher which can not be used, the message is empty when the matcher is usable.
	inline std::wstring DescribeMatcherError(std::shared_ptr<re2::RE2> const& matcher)
	{
		if (!matcher)
		{
			return L"the matcher of the config is empty";
		}
		if (!matcher->ok())
		{
			std::wstring error_message = L"the matcher of the config is not valid : <";
			Potato::Encode::UnicodeEncoder<char, wchar_t>::EncodeTo(
				std::string_view{ matcher->error() },
				std::back_inserter(error_message)
			);
			error_message += L">";
			return error_message;
		}
		return {};
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
}

export namespace Potato::Log
{
	template<LogLevel level>
	struct LogCategoryFormatter<UEBabyPram::InsightFilter::LogCategory, level>
	{
		template<typename OutputIterator, typename ...Parameters>
		OutputIterator operator()(OutputIterator iterator, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			return std::format_to(
				std::move(iterator),
				pattern,
				std::forward<Parameters>(parameters)...
			);
		}
	};

	template<LogLevel level>
	struct LogCategoryFormatter<UEBabyPram::InsightFilter::OutputCategory, level>
	{
		template<typename OutputIterator, typename ...Parameters>
		OutputIterator operator()(OutputIterator iterator, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			return std::format_to(
				std::move(iterator),
				pattern,
				std::forward<Parameters>(parameters)...
			);
		}
	};
}