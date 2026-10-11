module;

export module UEBabyPramInsightFilterParser;

import UEBabyPramInsightParser;
import Potato;
import std;

export namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
	
	struct Parser : public InsightParser::ParserInterface
	{
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