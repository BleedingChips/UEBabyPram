module;

module UEBabyPramLogCLI;

std::u8string_view general_help_string = u8R"(
UEBabyPramLogFilter - UE4 log filtering and formatting tool

Filters and formats Unreal Engine 4 .log files based on time ranges,
log levels, line numbers, category, and log content matching.

By default, filtered logs are saved in the same directory as the input file with a .filterout extension.

Commands:

  -h, --help [topic]           Show this help or detailed help for a specific topic

  -f, --file <path>            Add a .log file as input
                               Use -h file for detailed help

  -c, --condition <statement>  Add a filter condition (EBNF grammar)
                               Use -h condition for detailed help

  -p, --path <directory>       Scan directory for .log files and add as input
                               Use -h path for detailed help

  -oml, --output_mode_line     Output mode: normal with line numbers 
                               (mutually exclusive with -omtl, -omc)
                               Use -h output_mode_line for detailed help

  -omtl, --output_mode_only_time_and_line
                               Output mode: only time and line number 
                               (mutually exclusive with -oml, -omc)
                               Use -h output_mode_only_time_and_line for detailed help

  -osf, --output_separate_frame
                               Enable output with frame count separators
                               Use -h output_separate_frame for detailed help

  -e, --extension <ext>        Set custom output file extension
                               Use -h extension for detailed help

  -oc, --output_count <num>    Set maximum number of output log lines
                               Use -h output_count for detailed help

  -op, --out_path <directory>  Set output directory for filtered files
                               Use -h out_path for detailed help

  -ostd, --output_std          Output filtered results to stdout instead of file
                               Use -h output_std for detailed help

  -omc, --output_mode_custom <regex> <format>
                               Custom output mode with regex and format template 
                               (mutually exclusive with -oml, -omtl)
                               Use -h output_mode_custom for detailed help
)";



namespace UEBabyPram::LogFilter
{

	using namespace Potato;

	static void PrintGeneralHelp()
	{
		Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(
			general_help_string
		);
	}

	static void PrintCommandHelp(std::string_view topic)
	{
		if (topic == "h" || topic == "help")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -h, --help [topic]

    Shows general help overview, or detailed help for a specific topic.

    Usage:
      -h              Show general help with all commands
      -h file         Show detailed help for -f / --file
      -h condition    Show detailed help for -c / --condition
      -h path         Show detailed help for -p / --path
      -h output_mode_line       Show detailed help for -oml
      -h output_mode_only_time_and_line  Show detailed help for -omtl
      -h output_separate_frame  Show detailed help for -osf
      -h extension     Show detailed help for -e / --extension
      -h output_count  Show detailed help for -oc / --output_count
      -h out_path      Show detailed help for -op / --out_path
      -h output_std    Show detailed help for -ostd / --output_std
      -h output_mode_custom     Show detailed help for -omc
)");
		}
		else if (topic == "f" || topic == "file")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -f, --file <path>

    Adds a .log file as input. The file must exist and have a .log extension.

    Can be used multiple times to process multiple files.

    Usage:
      -f "C:\Logs\MyGame.log"
      -f "/path/to/UE4.log"

    Related: -p / --path to add all .log files in a directory.
)");
		}
		else if (topic == "c" || topic == "condition")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -c, --condition <statement>

    Adds a filter condition using EBNF grammar.
    Multiple conditions will be combined with Or.

    Syntax:
      Time <OP> <time>                     Filter by timestamp
      Level <OP> <level>                   Filter by log level
      Line <OP> <number>                   Filter by line number
      Message.<FUNC>("<string>")           Filter by log message content
      Category.<FUNC>("<string>")          Filter by log category
      (<cond>) && (<cond>)                 Logical AND
      (<cond>) & (<cond>)                  Logical AND
      (<cond>) || (<cond>)                 Logical OR
      (<cond>) | (<cond>)                  Logical OR
      !<cond>                              Logical NOT

    Comparison operators (<OP>):
      <  (less than)    <= (less or equal)    == (equal)
      >= (greater or equal)    >  (greater than)

    String functions (<FUNC>):
      StartWith  EndWith  Equal  Contains  Match (RE2 regex)

    Log levels (ordered low to high):
      VeryVerbose  Verbose  Log  Display  Warning  Error  Fatal

    Time formats:
      YYYY.MM.DD:HH.MM.SS:mmm      Full timestamp
      MM.DD:HH.MM.SS:mmm           Omit year
      DD:HH.MM.SS:mmm              Omit year and month
      HH.MM.SS                     Time only

    Examples:
      -c "Level >= Warning"
      -c "Time >= 2021.10.11:10.00.00:000"
      -c "Message.Contains(\"Error\") "
      -c "Category.Match(\"Log.*\") "
      -c "Line >= 100"
      -c "Level >= Error && Message.Contains(\"Fatal\") "
      -c "Time < 12.00.00:000"
)");
		}
		else if (topic == "p" || topic == "path")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -p, --path <directory>

    Scans a directory for all .log files (non-recursive) and adds them as input.

    The directory must exist.

    Usage:
      -p "C:\Logs\"
      -p "/var/log/ue4/"

    Related: -f / --file to add individual .log files.
)");
		}
		else if (topic == "oml" || topic == "output_mode_line")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -oml, --output_mode_line

    Sets output mode to normal output with line numbers prepended.

    Each output line will be prefixed with the original line number.

    Mutually exclusive with: -omtl, -omc

    Usage:
      -oml

	Example:
      Input: [2021.10.11-11.53.12:082][  0]LogConfig2: XXXX
      Output: Line-12:[2021.10.11-11.53.12:082][  0]LogConfig2: XXXX
)");
		}
		else if (topic == "omtl" || topic == "output_mode_only_time_and_line")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -omtl, --output_mode_only_time_and_line

    Sets output mode to display only the timestamp and line number.

    Mutually exclusive with: -oml, -omc

    Usage:
      -omtl

	Example:
      Input: [2021.10.11-11.53.12:082][  0]LogConfig2: XXXX
      Output: [Time:(2021.10.11-11.53.12:082) Line:(12)]
)");
		}
		else if (topic == "osf" || topic == "output_separate_frame")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -osf, --output_separate_frame

    Enables output with frame count separators between frames.

    Usage:
      -osf

	Example:
      Input: 
          [2021.10.11-11.53.12:082][  0]LogConfig2: XXXX
          [2021.10.11-11.53.12:092][  0]LogConfig2: XXXX
          [2021.10.11-11.53.12:102][  2]LogConfig2: XXXX
      Output:
          [2021.10.11-11.53.12:082][  0]LogConfig2: XXXX
          [2021.10.11-11.53.12:092][  0]LogConfig2: XXXX
          ===[1]Frame  [10]ms===
          [2021.10.11-11.53.12:102][  2]LogConfig2: XXXX
)");
		}
		else if (topic == "e" || topic == "extension")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -e, --extension <ext>

    Sets a custom output file extension for filtered output files.

    Default extension: .filterout

    Usage:
      -e ".filtered"
      -e ".txt"
)");
		}
		else if (topic == "oc" || topic == "output_count")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -oc, --output_count <num>

    Sets the maximum number of output log lines to produce.

    Once the limit is reached, remaining matching lines are discarded.

    Usage:
      -oc 100
      -oc 1000
)");
		}
		else if (topic == "op" || topic == "out_path")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -op, --out_path <directory>

    Sets the output directory for filtered files.

    The directory must exist. Each input .log file produces a corresponding
    filtered output file in this directory.

    Usage:
      -op "C:\Filtered\"
      -op "/tmp/output/"
)");
		}
		else if (topic == "ostd" || topic == "output_std")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -ostd, --output_std

    Outputs filtered results to stdout instead of writing to files.

    Usage:
      -ostd
)");
		}
		else if (topic == "omc" || topic == "output_mode_custom")
		{
			Log::Log<comment_log, Log::LogLevel::Display, u8"{}">(u8R"(
  -omc, --output_mode_custom <regex> <format>

    Sets custom output mode with a regex matcher and format template.
    Multiple -omc options can be combined, 
    each representing an independent output mode. 
    Lines matching the regex will be output in the corresponding format.

    Arguments:
      <regex>   RE2 regular expression to match against log lines
      <format>  Format template string with placeholders

    Format placeholders:
      {Time}       Timestamp of the log entry
      {Level}      Log level (VeryVerbose, Verbose, Log, Display, Warning, Error, Fatal)
      {Line}       Line number in the source file
      {Message}    Log message body text
      {Category}   Log category name
      {0} - {9}    Regex capture group references (numbered by opening paren)
      {{ }}        Escaped literal braces

    Mutually exclusive with: -oml, -omtl

    Usage:
      -omc "(\w+) " "{Time} [{Category}] {0}: {Log}"
      -omc "Error: (.+) " "ERROR | {Time} | {1}"

	Example:
      Command: -omc "Loc:\[X=([-0-9\.]+) Y=([-0-9\.]+) Z=([-0-9\.]+)\]" "(x={1} y={2} z={3}) "
      Input: [2021.10.11-11.53.12:082][  0]LogConfig2: Loc:[X=123 Y=124 Z=126]
      Output: (x=123 y=124 z=126)
)");
		}
		else
		{
			Log::Log<comment_log, Log::LogLevel::Error, L"Unknown topic <{}>, use -h to see available topics">(topic);
		}
	}

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor, LogFilterFormatter& fomatter)
	{
		for (std::size_t i = 1; i < argc; ++i)
		{
			std::string_view argv_string = argv[i];

			if (argv_string == "-h" || argv_string == "--help")
			{
				if (i + 1 < argc)
				{
					std::string_view next = argv[i + 1];
					if (next.starts_with("--"))
						next = next.substr(2);
					else if (next.starts_with("-"))
						next = next.substr(1);
					PrintCommandHelp(next);
					++i;
					return 1;
				}
				else
				{
					PrintGeneralHelp();
				}
				return 1;
			}

			if (argv_string == "-f" || argv_string == "--file")
			{
				if (i + 1 < argc)
				{
					std::u8string path;
					Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
					std::filesystem::path sub_argv = path;

					if (std::filesystem::exists(sub_argv) && std::filesystem::is_regular_file(sub_argv) && sub_argv.extension() == u8".log")
					{
						setting.input_file.emplace_back(std::move(sub_argv));
					}
					else {
						Log::Log<comment_log, Log::LogLevel::Error, L"File <{}> is not a acceptable file">(sub_argv.generic_wstring());
						return -1;
					}
					++i;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -f --file, use -h file for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-c" || argv_string == "--condition")
			{
				if (i + 1 < argc)
				{
					std::u8string error_message;
					std::string_view str = argv[i + 1];
					if (str.starts_with("\'") && str.ends_with("\'"))
					{
						str = str.substr(1, str.size() - 1);
					}
					if (!processor.AddStatement({ reinterpret_cast<char8_t const*>(str.data()), str.size() }))
					{
						Log::Log<comment_log, Log::LogLevel::Error, L"Invalid condition <{}>, use -h condition for detailed help">(str);
						return -1;
					}
					++i;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -c --condition, use -h condition for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-p" || argv_string == "--path")
			{
				if (i + 1 < argc)
				{
					std::u8string temp_path;
					Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ temp_path });
					std::filesystem::path sub_argv = temp_path;

					if (std::filesystem::is_directory(sub_argv))
					{
						for (auto& path_ite : std::filesystem::directory_iterator{ sub_argv })
						{
							if (std::filesystem::exists(path_ite) && std::filesystem::is_regular_file(path_ite) && path_ite.path().extension() == ".log")
							{
								setting.input_file.emplace_back(path_ite);
							}
						}
						++i;
					}
					else {
						Log::Log<comment_log, Log::LogLevel::Error, L"-p --path <path> : path should be a directory, which is <{}>">(sub_argv.generic_u16string());
						return -1;
					}
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -p --path, use -h path for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-oml" || argv_string == "--output_mode_line")
			{
				setting.mode = OutputMode::NORMAL_WITH_LINE;
			}
			else if (argv_string == "-omtl" || argv_string == "--output_mode_only_time_and_line")
			{
				setting.mode = OutputMode::ONLY_TIME_AND_LINE;
			}
			else if (argv_string == "-osf" || argv_string == "--output_separate_frame")
			{
				setting.output_with_separate_frame = true;
			}
			else if (argv_string == "-e" || argv_string == "--extension")
			{
				if (i + 1 < argc)
				{
					std::u8string path;
					Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
					std::filesystem::path sub_argv = path;
					setting.output_expand = sub_argv;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -e --extension, use -h extension for detailed help">();
					return -1;
				}
				++i;
			}
			else if (argv_string == "-oc" || argv_string == "--output_count")
			{
				if (i + 1 < argc)
				{
					std::string_view sub_argv = argv[i + 1];
					auto info = Potato::Format::DirectDeformat(sub_argv, setting.max_output_count);
					if (!info)
					{
						Log::Log<comment_log, Log::LogLevel::Error, L"-omc or --output_mode_count require a number">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -oc --output_count, use -h output_count for detailed help">();
					return -1;
				}
				++i;
			}
			else if (argv_string == "-op" || argv_string == "--out_path")
			{
				if (i + 1 < argc)
				{
					std::filesystem::path path = argv[i + 1];

					if (std::filesystem::is_directory(path))
					{
						setting.output_path = path;
						++i;
					}
					else {
						Log::Log<comment_log, Log::LogLevel::Error, L"-op --out_path <path> : path should be a directory, which is <{}>">(path.generic_u16string());
						return -1;
					}
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -op --out_path, use -h out_path for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-ostd" || argv_string == "--output_std")
			{
				setting.target = UEBabyPram::LogFilter::OutputTarget::STD;
			}
			else if (argv_string == "-omc" || argv_string == "--output_mode_custom")
			{
				setting.mode = OutputMode::CUSTOM;
				if (i + 2 < argc)
				{
					std::u8string error_message;
					std::string_view reg_format = argv[i + 1];
					std::string_view format_format = argv[i + 2];
					std::u8string_view reg_format_u8{ reinterpret_cast<char8_t const*>(reg_format.data()), reg_format.size() };
					std::u8string_view format_format_u8{ reinterpret_cast<char8_t const*>(format_format.data()), format_format.size() };
					if (!fomatter.AddStatement(reg_format_u8, format_format_u8, error_message))
					{
						Log::Log<comment_log, Log::LogLevel::Error, L"Invalid custom format <{}>, use -h output_mode_custom for detailed help">(error_message);
						return -1;
					}
					i += 2;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -omc --output_mode_custom, use -h output_mode_custom for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-p" || argv_string == "--path")
			{
				if (i + 1 < argc)
				{
					std::filesystem::path path = argv[i + 1];

					if (std::filesystem::is_directory(path))
					{
						for (auto& path_ite : std::filesystem::directory_iterator{ path })
						{
							if (std::filesystem::exists(path_ite) && std::filesystem::is_regular_file(path_ite) && path_ite.path().extension() == ".log")
							{
								setting.input_file.emplace_back(path_ite);
							}
						}
						++i;
					}
					else {
						Log::Log<comment_log, Log::LogLevel::Error, L"-p --path <path> : path should be a directory, which is <{}>">(path.generic_u16string());
						return -1;
					}
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -p --path, use -h path for detailed help">();
					return -1;
				}
			}
		}
		return 0;
	}
}