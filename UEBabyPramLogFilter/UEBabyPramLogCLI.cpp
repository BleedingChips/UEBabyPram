module;

module UEBabyPramLogCLI;

namespace
{
	// The version of the tool, which -v --version reports. It is kept in the command line of this tool only, so the
	// insight filter carries its own version and a new version of one tool never rebuilds the other one.
	constexpr std::string_view version = "1.2";
}

std::u8string_view general_help_string = u8R"(
UEBabyPramLogFilter - filter an Unreal Engine .log file

Usage:
  UEBabyPramLogFilter -f <log> [-c <condition>]... [-o <output>] [-oml] [-osf] [-or <min> <max>]

  Without -o the run only counts the matches and prints the summary; no log is written.
  With -o the matched lines are written to <output>.
  The summary on stdout is: target log, output log (with -o), matched count, first/last time,
  and first/last matched line number.

Commands:
  -h, --help [condition]       Show this help, or the condition syntax with -h condition
  -v, --version                Show the version of the tool

  -f, --file <path>            The one .log file to read
  -c, --condition <statement>  Add a filter condition; repeat for OR, use & and | inside one condition
  -o, --output <path>          Write the matched lines into this file

  -oml, --output_mode_line     Prefix every written line with line-<n>:
  -osf, --output_separate_frame
                               Insert a frame separator when the frame count changes
  -or, --output_range <min> <max>
                               Write the matched lines whose output index is in [min, max), 0-based
)";

std::u8string_view condition_help_string = u8R"(
Condition syntax:
  Time <OP> <time>
  Level <OP> <level>
  Line <OP> <number>
  Message.<FUNC>(^string^)
  Category.<FUNC>(^string^)
  (<cond>) & (<cond>)
  (<cond>) | (<cond>)
  !<cond>

  <OP>   <  <=  ==  >=  >
  <FUNC> StartWith  EndWith  Equal  Contains  Match

  Match uses RE2 and is not anchored. The other functions compare the text exactly, case sensitively.
  A string is written between ^ and ^, and a literal ^ is written as ^^.

  Levels, from low to high:
    VeryVerbose  Verbose  Log  Display  Warning  Error  Fatal

  Time formats:
    YYYY.MM.DD-HH.MM.SS:mmm
    YYYY.MM.DD-HH.MM.SS
    HH.MM.SS:mmm
    HH.MM.SS

  Every -c is combined with OR. Write a range inside one condition:
    -c "Time >= 2021.10.11-10.00.00:000 & Time < 2021.10.11-12.00.00:000"
)";

namespace UEBabyPram::LogFilter
{
	using namespace Potato;

	static void PrintGeneralHelp()
	{
		LogToConsole<Log::LogLevel::Display, u8"{}">(general_help_string);
	}

	static void PrintConditionHelp()
	{
		LogToConsole<Log::LogLevel::Display, u8"{}">(condition_help_string);
	}

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor)
	{
		bool file_given = false;
		bool output_given = false;
		bool line_mode_given = false;
		bool frame_given = false;
		bool range_given = false;

		for (std::size_t i = 1; i < argc; ++i)
		{
			std::string_view argv_string = argv[i];

			if (argv_string == "-v" || argv_string == "--version")
			{
				LogToConsole<Log::LogLevel::Display, L"UEBabyPramLogFilter <{}>">(version);
				return 1;
			}

			if (argv_string == "-h" || argv_string == "--help")
			{
				if (i + 1 < argc)
				{
					std::string_view next = argv[i + 1];
					if (next == "condition" || next == "-condition" || next == "--condition")
					{
						PrintConditionHelp();
						++i;
						return 1;
					}
				}

				PrintGeneralHelp();
				return 1;
			}

			if (argv_string == "-f" || argv_string == "--file")
			{
				if (file_given)
				{
					LogToConsole<Log::LogLevel::Error, L"-f --file accepts exactly one file">();
					return -1;
				}

				if (i + 1 < argc)
				{
					std::u8string path;
					Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
					std::filesystem::path sub_argv = path;

					if (std::filesystem::exists(sub_argv) && std::filesystem::is_regular_file(sub_argv) && sub_argv.extension() == u8".log")
					{
						setting.input_file = std::move(sub_argv);
						file_given = true;
					}
					else {
						LogToConsole<Log::LogLevel::Error, L"File <{}> is not an acceptable .log file">(sub_argv.generic_wstring());
						return -1;
					}
					++i;
				}
				else {
					LogToConsole<Log::LogLevel::Error, L"Unsupport command -f --file, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-c" || argv_string == "--condition")
			{
				if (i + 1 < argc)
				{
					std::pmr::u8string error_message;
					std::string_view str = argv[i + 1];
					if (str.starts_with("\'") && str.ends_with("\'"))
					{
						str = str.substr(1, str.size() - 1);
					}
					if (!processor.AddStatement({ reinterpret_cast<char8_t const*>(str.data()), str.size() }))
					{
						LogToConsole<Log::LogLevel::Error, L"Invalid condition <{}>, use -h condition for detailed help">(str);
						return -1;
					}
					++i;
				}
				else {
					LogToConsole<Log::LogLevel::Error, L"Unsupport command -c --condition, use -h condition for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-o" || argv_string == "--output")
			{
				if (output_given)
				{
					LogToConsole<Log::LogLevel::Error, L"-o --output accepts exactly one file">();
					return -1;
				}

				if (i + 1 < argc)
				{
					std::u8string path;
					Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
					std::filesystem::path output_file = path;
					std::filesystem::path parent = output_file.parent_path();

					if (parent.empty() || std::filesystem::is_directory(parent))
					{
						setting.output_file = std::move(output_file);
						output_given = true;
					}
					else {
						LogToConsole<Log::LogLevel::Error, L"-o --output <path> : the directory of <{}> does not exist">(output_file.generic_wstring());
						return -1;
					}
					++i;
				}
				else {
					LogToConsole<Log::LogLevel::Error, L"Unsupport command -o --output, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-oml" || argv_string == "--output_mode_line")
			{
				setting.mode = OutputMode::NORMAL_WITH_LINE;
				line_mode_given = true;
			}
			else if (argv_string == "-osf" || argv_string == "--output_separate_frame")
			{
				setting.output_with_separate_frame = true;
				frame_given = true;
			}
			else if (argv_string == "-or" || argv_string == "--output_range")
			{
				if (i + 2 < argc)
				{
					std::size_t min = 0;
					std::size_t max = 0;
					std::string_view sub_argv = argv[i + 1];
					std::string_view sub_argv2 = argv[i + 2];
					auto info = Potato::Format::DirectDeformat(sub_argv, min);
					auto info2 = Potato::Format::DirectDeformat(sub_argv2, max);
					if (!info || !info2 || min > max)
					{
						LogToConsole<Log::LogLevel::Error, L"-or --output_range requires a valid range (min <= max)">();
						return -1;
					}
					setting.output_span = { min, max };
					range_given = true;
				}
				else {
					LogToConsole<Log::LogLevel::Error, L"Unsupport command -or --output_range, use -h for detailed help">();
					return -1;
				}
				i += 2;
			}
			else
			{
				LogToConsole<Log::LogLevel::Error, L"Unsupport command {}, use -h for detailed help">(argv_string);
				return -1;
			}
		}

		if (!file_given)
		{
			LogToConsole<Log::LogLevel::Error, L"Require a target file with -f --file, use -h for detailed help">();
			return -1;
		}

		if (!output_given && (line_mode_given || frame_given || range_given))
		{
			LogToConsole<Log::LogLevel::Error, L"-oml, -osf and -or only apply to the written log, so they require -o --output">();
			return -1;
		}

		return 0;
	}
}
