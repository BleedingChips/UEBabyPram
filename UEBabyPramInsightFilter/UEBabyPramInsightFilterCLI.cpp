module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterCLI;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThreadParser;
import UEBabyPramInsightFilterEventSearchParser;
import UEBabyPramInsightFilterEventStatisticParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace Potato;

	namespace
	{
		void PrintGeneralHelp()
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Display, LR"(
UEBabyPramInsightFilter, report the GameThread timing of an unreal insight .utrace file,
search the events of the trace by a regular expression of the event name, or report the
timing statistic of every event whose name is matched by a regular expression.

Usage:
  UEBabyPramInsightFilter -f <path of the .utrace file> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -r <regex> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -es <regex> [options]

Options:
  -f, --file <path>
      Path of the unreal insight .utrace file which should be analyzed. Required.

  -r, --regex <regex>
      Regular expression of the event name, in the re2 (RE2) syntax.
      The expression has to match the whole event name,
      ex: -r "FEngineLoop::.*" or -r ".*Tick.*".
      When it is set, every event whose name is matched is reported with the file path and
      the line where it is defined, otherwise the GameThread timing analysis is reported.
      The same event name may be defined in several places, so every location is reported.
      -r and -es can not be used at the same time.

  -es, --event_stat <regex>
      Regular expression of the event name, in the re2 (RE2) syntax, the same syntax as -r.
      When it is set, the timing of every event whose name is matched is collected and
      reported per event:
        - the occurrence count, the mean duration and the mean exclude time,
        - the median and the standard deviation of the duration of the single occurrence,
        - the top longest occurrences with the call chain which leads to them.
      The exclude time is the duration of the occurrence without the time of its child events.

  -mlc, --max_location_count <count>
      How many matched event locations should be reported with -r, 0 for no limit, default is 256.

  -tx, --top_count <count>
      How many of the longest occurrences should be reported with -es, default is 5.

  -mrf, --max_record_frame <count>
      How many of the longest game frames should be recorded and reported, default is 10.

  -metc, --max_exclude_time_count <count>
      How many of the longest exclude time events should be kept in every top list, default is 30.

  -sr, --sampling_ratio <ratio>
      Sampling ratio of the analyzed time range, a value in [0.0, 1.0], default is 0.8.

  -h, --help
      Print this help.
)">();
		}
	}

	int HandleCommand(int argc, char* argv[], CommandLineSetting& setting)
	{
		GameThreadParser::Config config;
		EventSearchParser::Config search_config;
		EventStatisticParser::Config statistic_config;
		bool search_event = false;
		bool statistic_event = false;
		std::u8string path_buffer;

		for (std::size_t i = 1; i < argc; ++i)
		{
			std::string_view argv_string = argv[i];

			if (argv_string == "-h" || argv_string == "--help")
			{
				PrintGeneralHelp();
				return 1;
			}
			else if (argv_string == "-f" || argv_string == "--file")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });
					std::filesystem::path sub_argv = path_buffer;

					if (std::filesystem::exists(sub_argv) && std::filesystem::is_regular_file(sub_argv))
					{
						setting.insight_path = std::move(sub_argv);
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-f --file <path> : <{}> is not an acceptable file, use -h for detailed help">(sub_argv.generic_wstring());
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -f --file, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-r" || argv_string == "--regex")
			{
				if (i + 1 < argc)
				{
					std::string_view pattern = argv[i + 1];
					re2::RE2::Options options;
					// The error is reported below, re2 should not print it a second time.
					options.set_log_errors(false);
					auto matcher = std::make_shared<re2::RE2>(pattern, options);
					if (matcher->ok())
					{
						search_config.matcher = std::move(matcher);
						search_event = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-r --regex <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -r --regex, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mlc" || argv_string == "--max_location_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						search_config.max_location_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mlc --max_location_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mlc --max_location_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-es" || argv_string == "--event_stat")
			{
				if (i + 1 < argc)
				{
					std::string_view pattern = argv[i + 1];
					re2::RE2::Options options;
					// The error is reported below, re2 should not print it a second time.
					options.set_log_errors(false);
					auto matcher = std::make_shared<re2::RE2>(pattern, options);
					if (matcher->ok())
					{
						statistic_config.matcher = std::move(matcher);
						statistic_event = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-es --event_stat <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -es --event_stat, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-tx" || argv_string == "--top_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						statistic_config.top_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-tx --top_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -tx --top_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mrf" || argv_string == "--max_record_frame")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						config.max_record_frame = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mrf --max_record_frame <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mrf --max_record_frame, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-metc" || argv_string == "--max_exclude_time_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						config.max_exclude_time_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-metc --max_exclude_time_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -metc --max_exclude_time_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-sr" || argv_string == "--sampling_ratio")
			{
				if (i + 1 < argc)
				{
					double value = 0.0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value) && value >= 0.0 && value <= 1.0)
					{
						config.sampling_ratio = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-sr --sampling_ratio <ratio> : <{}> is not a valid ratio in [0.0, 1.0], use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -sr --sampling_ratio, use -h for detailed help">();
					return -1;
				}
			}
			else
			{
				Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command <{}>, use -h to see the available commands">(argv_string);
				return -1;
			}
		}

		if (statistic_event && search_event)
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-r --regex and -es --event_stat can not be used at the same time, use -h for detailed help">();
			return -1;
		}

		if (statistic_event)
		{
			setting.parser = std::make_unique<EventStatisticParser>(std::move(statistic_config));
		}
		else if (search_event)
		{
			setting.parser = std::make_unique<EventSearchParser>(std::move(search_config));
		}
		else {
			setting.parser = std::make_unique<GameThreadParser>(config);
		}

		return 0;
	}
}
