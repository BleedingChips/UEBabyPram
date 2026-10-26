module;

#include <cassert>

export module UEBabyPramLogCLI;

import std;
import Potato;
import UEBabyPramLogFilter;


export namespace UEBabyPram::LogFilter
{
	using namespace Potato;

	enum class OutputTarget
	{
		FILE,
		STD,
	};

	enum class OutputMode
	{
		NORMAL,
		NORMAL_WITH_LINE,
		ONLY_TIME_AND_LINE,
		CUSTOM
	};

	struct FilterSetting
	{
		std::pmr::vector<std::filesystem::path> input_file;
		std::filesystem::path output_path;
		std::filesystem::path output_expand = ".filterout";
		OutputTarget target = OutputTarget::FILE;
		OutputMode mode = OutputMode::NORMAL;
		bool output_with_separate_frame = false;
		Potato::Misc::IndexSpan<> output_span = { 0, std::numeric_limits<std::size_t>::max() };
		bool not_repeat = false;
	};

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor, LogFilterFormatter& fomatter);

	// The log messages of the command line and of the filter itself are printed without the time and
	// without the name of the category, and the level is only printed for an error, so the help text
	// and the filtered output stay readable.
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

	// Whether the messages below the error level are dropped.
	bool IsConsoleLogSuppressed();
	// Drop the messages below the error level, or stop dropping them. The filter drops them when the
	// filtered log is printed to the standard output, so the messages never pollute that output.
	void SetConsoleLogSuppressed(bool suppressed);

	// Print one message of the command line or of the filter, see CommandLineLogFormatter.
	template<Potato::Log::LogLevel level, Potato::TMP::TypeString pattern, typename ...Parameters>
	void LogToConsole(Parameters&& ...parameters)
	{
		if (level <= Potato::Log::LogLevel::Log && IsConsoleLogSuppressed())
		{
			return;
		}

		Potato::Log::LogTo<CommandLineLogFormatter<level>, pattern>(
			*Potato::Log::GetSTDOutPrinter(),
			std::forward<Parameters>(parameters)...
		);
	}
}
