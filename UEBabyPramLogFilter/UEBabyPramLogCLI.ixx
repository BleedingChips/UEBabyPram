module;

#include <cassert>

export module UEBabyPramLogCLI;

import std;
import Potato;
import UEBabyPramLogFilter;


export namespace UEBabyPram::LogFilter
{
	using namespace Potato;

	enum class OutputMode
	{
		NORMAL,
		NORMAL_WITH_LINE,
	};

	struct FilterSetting
	{
		// The one input file of the run.
		std::filesystem::path input_file;
		// The file which the matched lines are written into, empty when the run only counts the matches and
		// writes no log.
		std::filesystem::path output_file;
		OutputMode mode = OutputMode::NORMAL;
		bool output_with_separate_frame = false;
		Potato::Misc::IndexSpan<> output_span = { 0, std::numeric_limits<std::size_t>::max() };
	};

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor);

	// The log messages of the command line and of the filter itself are printed without the time and
	// without the name of the category, and the level is only printed for an error, so the help text
	// stays readable.
	template<Potato::Log::LogLevel level>
	struct CommandLineLogFormatter
	{
		template<typename OutIterator, typename ...Parameters>
		static OutIterator LogFormat(OutIterator iterator, Potato::Log::FormatedSystemTime, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			if constexpr (level == Potato::Log::LogLevel::Error)
			{
				iterator = std::format_to(
					std::move(iterator),
					L"{}:",
					level
				);
			}

			return std::format_to(
				std::move(iterator),
				pattern,
				std::forward<Parameters>(parameters)...
			);
		}

		static constexpr Potato::Log::LogLevel value = level;
	};

	// Print one message of the command line or of the filter, see CommandLineLogFormatter.
	template<Potato::Log::LogLevel level, Potato::TMP::TypeString pattern, typename ...Parameters>
	void LogToConsole(Parameters&& ...parameters)
	{
		Potato::Log::LogTo<CommandLineLogFormatter<level>, pattern>(
			*Potato::Log::GetSTDOutPrinter(),
			std::forward<Parameters>(parameters)...
		);
	}
}
