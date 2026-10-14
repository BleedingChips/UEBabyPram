module;

#include <cassert>

module UEBabyPramInsightFilterCLI;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThreadParser;
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
UEBabyPramInsightFilter, report the GameThread timing of an unreal insight .utrace file.

Usage:
  UEBabyPramInsightFilter -f <path of the .utrace file> [options]

Options:
  -f, --file <path>
      Path of the unreal insight .utrace file which should be analyzed. Required.

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

		setting.parser = std::make_unique<GameThreadParser>(config);

		return 0;
	}
}
