module;

module UEBabyPramLogCLI;

namespace UEBabyPram::LogFilter
{

	using namespace Potato;

	static void PrintGeneralHelp()
	{
		Log::Log < comment_log, Log::LogLevel::Display,
			LR"(Usage: UEBabyPramLogFilter [OPTIONS]

  By default (filter mode), filtered output is written to a file in the same
  directory as the input file. Use -fmf or -fml to redirect output to STDOUT
  instead.

Options:
  -h, --help [TOPIC]                    Show this help message. With a TOPIC argument,
                                        show detailed help for that command.
                                        TOPIC can be a short name, or prefixed with
                                        - or -- (e.g. -c, --condition).
                                        Available topics: file, condition, path,
                                        output_mode_line, output_mode_separate_frame,
                                        extension, find_mode_first, find_mode_last,
                                        find_mode_count.
  
  -f, --file <path>                     Specify input .log file(s). Can be specified
                                        multiple times.
  
  -c, --condition <expr>                Filter condition expression (see -h -c for more
                                        infonmation). Can be specified multiple times.
  
  -p, --path <directory>                Scan all .log files in the given directory.
  
  -oml, --output_mode_line              Prepend each output line with its line number.
  
  -omsf, --output_mode_separate_frame   Insert frame separation markers when frame
                                        count changes.
 
  -e, --extension <ext>                 Custom output file extension.
  
  -fmf, --find_mode_first               Find first N matching entries (requires -fmc).
                                        This enables FindMode: output goes to STDOUT
                                        in the following format:
                                        File:<FilePath> : [Time:(year.month.day:hour.minute.second:millisecond) Line:(number)],...
                                        Example:
                                        File:<C:\Log.log> : [Time(2006.11.11:11.23.34:123) Line(123)],...
  
  -fml, --find_mode_last                Find last N matching entries (requires -fmc).
                                        This enables FindMode: output goes to STDOUT
                                        in the following format:
                                        File:<FilePath> : [Time:(year.month.day:hour.minute.second:millisecond) Line:(number)],...
                                        Example:
                                        File:<C:\Log.log> : [Time(2006.11.11:11.23.34:123) Line(123)],...
  
  -fmc, --find_mode_count <N>           Number of entries for find mode (default: 40).
)"
		> ();
	}

	static void PrintCommandHelp(std::string_view topic)
	{
		if (topic == "file" || topic == "f")
		{
			std::print(
				R"(-f, --file <path>

  Specify one or more input .log files. This option can be repeated to
  specify multiple files. Each file is processed in parallel using
  multiple threads.

  The path must point to an existing regular file with a .log extension.

  Example:
    UEBabyPramLogFilter -f output.log
    UEBabyPramLogFilter -f a.log -f b.log

)");
		}
		else if (topic == "condition" || topic == "c")
		{
			std::print(
				R"(-c, --condition <expr>

  Add a filter condition expressed in the following EBNF grammar.
  This option can be specified multiple times; all conditions are
  AND-combined.

  EBNF Grammar:
	%s

  COMPARE operators:
    <       Smaller (for string: StartWith)
    <=      SmallerEqual (for string: EndWith)
    ==      Equal
    >=      BiggerEqual (for string: Contains)
    >       Bigger (for string: regex/pattern match via DFA)

  TIME format:
    year.month.day:hour.minute.second:millisecond
    year.month.day:hour.minute.second
    year.month.day:hour.minute
    year.month.day

  LOGLEVEL (ordered by severity):
    Fatal(0) > Error(1) > Warning(2) > Display(3) > Log(4) > Verbose(5) > VeryVerbose(6)

  STRING_COMPARE:
    StartWith  - check if log content starts with a substring (<)
    EndWith    - check if log content ends with a substring (<=)
    Equal      - exact match (==)
    Contains   - check if log content contains a substring (>=)
    HeadMatchs - regex/pattern match using DFA (>)

  Examples:
    -c "Level >= Warning"         -- only Warning and above
    -c "Time > 2024.6.1"          -- logs after June 1, 2024
    -c "Line > 500"               -- logs after line 500
    -c "Log.Contains(\"Error\")\"   -- log text contains \"Error\"
    -c "Category.StartWith(\"Log\")\"
    -c \"(Level >= Error) && (Log.Contains(\"Assert\"))\"

)",
Potato::Log::AddLogStringWrapper(UEBabyPram::LogFilter::GetEbnfString())
);
		}
		else if (topic == "path" || topic == "p")
		{
			std::print(
				R"(-p, --path <directory>

  Scan all .log files in the given directory. Every regular file with
  a .log extension under the directory will be added as an input file.

  Example:
    UEBabyPramLogFilter -p "C:\Logs\"

)");
		}
		else if (topic == "output-mode-line" || topic == "oml")
		{
			std::print(
				R"(-oml, --output_mode_line

  Prepend each output line with its original line number in the
  format "Line-NNN:<log content>".

)");
		}
		else if (topic == "output-mode-separate-frame" || topic == "omsf")
		{
			std::print(
				R"(-omsf, --output_mode_separate_frame

  Insert a frame separation marker when the frame count changes between
  consecutive log lines. The marker shows the frame delta and elapsed
  time:
    =====[N]Frame  [M]ms=====

)");
		}
		else if (topic == "extension" || topic == "e")
		{
			std::print(
				R"(-e, --extension <ext>

  Specify a custom output file extension. If not set, the default
  suffix ".filterout" is appended to the input file name.

  Example:
    UEBabyPramLogFilter -f output.log -e ".filtered"

)");
		}
		else if (topic == "find-mode-first" || topic == "fmf")
		{
			std::print(
				R"(-fmf, --find_mode_first

  Instead of writing filtered output to a file, find the first N
  matching log entries and print them to stdout. Use -fmc to set N.

  Example:
    UEBabyPramLogFilter -f output.log -c "Level >= Error" -fmf -fmc 10

)");
		}
		else if (topic == "find-mode-last" || topic == "fml")
		{
			std::print(
				R"(-fml, --find_mode_last

  Instead of writing filtered output to a file, find the last N
  matching log entries and print them to stdout. Use -fmc to set N.

  Example:
    UEBabyPramLogFilter -f output.log -c \"Log.Contains(^crash^)\" -fml -fmc 5

)");
		}
		else if (topic == "find-mode-count" || topic == "fmc")
		{
			std::print(
				R"(-fmc, --find_mode_count <N>

  Set the number of entries for find mode (used with -fmf or -fml).
  Default value is 40.

  Example:
    UEBabyPramLogFilter -f output.log -c "Level >= Error" -fmf -fmc 20

)");
		}
		else
		{
			std::print("Unknown help topic: {}", topic);
			PrintGeneralHelp();
		}
	}

	int HandleComment(int argc, char* argv[], FilterSetting& setting, LogFilterProcessor& processor, LogFilterFormatter& fomatter)
	{
		for (std::size_t i = 0; i < argc; ++i)
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
					return 0;
				}
				else
				{
					PrintGeneralHelp();
				}
				return 0;
			}

			if (argv_string == "-f" || argv_string == "--file")
			{
				if (i + 1 < argc)
				{
					std::filesystem::path sub_argv = argv[i + 1];

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
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -f --file, see -h or --help for more infomation">();
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
						Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -c --condition {}, see -h or --help for more infomation">(str);
						return -1;
					}
					++i;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -c --condition, see -h or --help for more infomation">();
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
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -p --path, see -h or --help for more infomation">();
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
			else if (argv_string == "-omsf" || argv_string == "--output_mode_separate_frame")
			{
				setting.output_with_separate_frame = true;
			}
			else if (argv_string == "-e" || argv_string == "--extension")
			{
				if (i + 1 < argc)
				{
					std::filesystem::path sub_argv = argv[i + 1];
					setting.output_expand = sub_argv;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -e --extension, see -h or --help for more infomation">();
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
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -omc --output_mode_count, see -h or --help for more infomation">();
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
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -op --out_path, see -h or --help for more infomation">();
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
					if (!fomatter.AddStatement(reg_format, format_format, error_message))
					{
						Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -omc --output_mode_custom, see -h or --help for more infomation. : <{}>">(error_message);
						return -1;
					}
					i += 2;
				}
				else {
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -omc --output_mode_custom, see -h or --help for more infomation">();
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
					Log::Log<comment_log, Log::LogLevel::Error, L"Unsupport command -p --path, see -h or --help for more infomation">();
					return -1;
				}
			}
		}
		return 0;
	}
}