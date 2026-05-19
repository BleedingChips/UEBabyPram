import UEBabyPram;
import UEBabyPramLogFilter;
import Potato;
import std;

constexpr auto log_filter = Potato::TMP::TypeString(u8"LogFilter");

namespace Potato::Log
{
	template<Potato::Log::LogLevel level>
	struct LogCategoryFormatter<log_filter, level>
	{
		template<typename OutputIterator, typename ...Parameters>
		OutputIterator operator()(OutputIterator iterator, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			if constexpr (level != Potato::Log::LogLevel::Display && level != Potato::Log::LogLevel::Error)
			{
				Potato::Log::FormatedSystemTime time;
				iterator = std::format_to(
					std::move(iterator),
					L"[{}]{}<{}>:",
					time, log_filter, level
				);
			}
			else if constexpr (level == Potato::Log::LogLevel::Error)
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
	};
}

static bool forbid_log = false;

namespace Potato::Log
{
	template<>
	struct LogCategoryProperty<log_filter>
	{
		static bool IsLogEnable(LogLevel level) 
		{
			if (forbid_log)
			{
				return level > LogLevel::Log;
			}
			else {
				return true;
			}
		}
	};
}


using namespace UEBabyPram;
using namespace Potato;

int main(int argc, char* argv[])
{
	
	UEBabyPram::LogFilter::FilterSetting setting;
	UEBabyPram::LogFilter::LogFilterProcessor processor;

	for (std::size_t i = 0; i < argc; ++i)
	{
		std::string_view argv_string = argv[i];
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
					Log::Log<log_filter, Log::LogLevel::Error, L"File <{}> is not a acceptable file">(sub_argv.generic_wstring());
					return -1;
				}
				++i;
			}
			else {
				Log::Log<log_filter, Log::LogLevel::Error, L"Unsupport command -f --file, see -h or --help for more infomation">();
				return -1;
			}
		}
		else if (argv_string == "-c" || argv_string == "--condition")
		{
			if (i + 1 < argc)
			{
				std::u8string error_message;
				std::string_view str = argv[i + 1];
				if (!processor.AddStatement({ reinterpret_cast<char8_t const*>(str.data()), str.size() }))
				{
					return -1;
				}
				++i;
			}
			else {
				Log::Log<log_filter, Log::LogLevel::Error, L"Unsupport command -c --condition, see -h or --help for more infomation">();
				return -1;
			}
		}
		else if (argv_string == "-p" || argv_string == "--path")
		{
			if (i + 1 < argc)
			{
				std::string_view str = argv[i + 1];

				for (auto& path_ite : std::filesystem::directory_iterator{ str })
				{
					if (std::filesystem::exists(path_ite) && std::filesystem::is_regular_file(path_ite) && path_ite.path().extension() == ".log")
					{
						setting.input_file.emplace_back(path_ite);
					}
				}
				++i;
			}
			else {
				Log::Log<log_filter, Log::LogLevel::Error, L"Unsupport command -p --path, see -h or --help for more infomation">();
				return -1;
			}
		}
		else if (argv_string == "-oml" || argv_string == "--output_mode_line")
		{
			setting.output_with_line = true;
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
				Log::Log<log_filter, Log::LogLevel::Error, L"Unsupport command -e --extension, see -h or --help for more infomation">();
				return -1;
			}
			++i;
		}
		else if (argv_string == "-fmf" || argv_string == "--find_mode_first")
		{
			setting.find_mode = UEBabyPram::LogFilter::FindMode::First;
		}
		else if (argv_string == "-fml" || argv_string == "--find_mode_last")
		{
			setting.find_mode = UEBabyPram::LogFilter::FindMode::Last;
		}
		else if (argv_string == "-fmc" || argv_string == "--find_mode_count")
		{
			if (i + 1 < argc)
			{
				std::string_view sub_argv = argv[i + 1];
				auto info = Potato::Format::DirectDeformat(sub_argv, setting.find_count);
				if(!info)
				{
					Log::Log<log_filter, Log::LogLevel::Error, L"-fml --find_mode_last require a number">(sub_argv);
					return -1;
				}
			}
			else {
				Log::Log<log_filter, Log::LogLevel::Error, L"Unsupport command -fml --find_mode_last, see -h or --help for more infomation">();
				return -1;
			}
			++i;
		}
	}

	if (setting.input_file.empty())
	{
		Log::Log<log_filter, Log::LogLevel::Error, L"Require an target file with -f or --file, see -h or --help for more infomation">();
		return -1;
	}

	if (setting.find_mode != UEBabyPram::LogFilter::FindMode::None)
	{
		forbid_log = true;
	}

	Potato::Task::Context context;

	for (auto& file_path : setting.input_file)
	{
		context.Commit(
			[&, file_path](Potato::Task::Context&, Potato::Task::Node::Parameter&, Potato::Task::Node&) {
				Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Start Filter file:<{}>">(file_path.generic_u8string());
				Potato::Document::DocumentReader reader(file_path);
				if (!reader)
				{
					if (std::filesystem::exists(file_path))
					{
						auto temporary_path = std::filesystem::temp_directory_path();
						temporary_path += file_path.filename();
						std::filesystem::copy_file(
							file_path,
							temporary_path
						);
						reader.Open(temporary_path);
					}
				}

				if (reader)
				{

					std::filesystem::path out_path = setting.output_file;
					if (out_path.empty())
					{
						out_path = file_path;
						if (!setting.output_expand.empty())
						{
							out_path += setting.output_expand;
						}
						else {
							out_path += L".filterout";
						}
					}

					Potato::Document::PlainTextReader::Config config;
					config.cache_buffer_size = std::min(static_cast<std::size_t>(1024) * 1024 * 20, reader.GetStreamSize());
					Potato::Document::PlainTextReader plain_reader(reader, config);
					Potato::Document::DocumentWriter writter;
					if (setting.find_mode == UEBabyPram::LogFilter::FindMode::None)
					{
						writter.Open(out_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
					}
					Potato::Document::PlainTextWritter::Config writer_config;
					config.bom = Potato::Document::BomT::UTF8;
					Potato::Document::PlainTextWritter plain_writer(writter, writer_config);

					UEBabyPram::LogParser::LogLine::TimeT last_frame_time;
					std::optional<std::size_t> last_frame_count;
					std::string temp_output;
					std::chrono::system_clock::time_point last_log_time = std::chrono::system_clock::now();
					Potato::Reg::DfaProcessor dfa_processor;
					struct LineCount
					{
						std::string string;
					};
					std::pmr::deque<LineCount> line_record;
					UEBabyPram::LogParser::ForeachLogLine(plain_reader, [&](UEBabyPram::LogParser::LogLine log_line) -> bool {

						auto now = std::chrono::system_clock::now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time);
						if (dur > std::chrono::seconds{ 5 })
						{
							last_log_time = now;
							Log::Log<log_filter, Log::LogLevel::Log, u"Filtering log line: {}">(log_line.line.Begin());
						}

						if (processor)
						{
							auto re = processor.Detect(log_line, dfa_processor);

							if (!re.has_value() || !*re)
								return true;
						}

						if (setting.find_mode != UEBabyPram::LogFilter::FindMode::None)
						{
							std::string string;
							std::format_to(
								std::back_insert_iterator(string),
								"[Time:({}.{}.{}:{}.{}.{}:{}) Line:({})]",
								Potato::Log::AddLogStringWrapper(log_line.property.time.year),
								Potato::Log::AddLogStringWrapper(log_line.property.time.month),
								Potato::Log::AddLogStringWrapper(log_line.property.time.day),
								Potato::Log::AddLogStringWrapper(log_line.property.time.hour),
								Potato::Log::AddLogStringWrapper(log_line.property.time.minute),
								Potato::Log::AddLogStringWrapper(log_line.property.time.second),
								Potato::Log::AddLogStringWrapper(log_line.property.time.millisecond),
								log_line.line.Begin()
							);
							line_record.emplace_back(std::move(string));

							if (line_record.size() >= setting.find_count)
							{
								if (setting.find_mode == UEBabyPram::LogFilter::FindMode::First)
									return false;
								else
									line_record.pop_front();
							}

							return true;
						}

						if (setting.output_with_separate_frame)
						{
							auto fc = log_line.GetFrameCount();
							auto time = log_line.GetSystemClockTimePoint();

							if (fc.has_value())
							{
								if (last_frame_count.has_value())
								{
									if (*fc != *last_frame_count)
									{
										std::int64_t count = static_cast<std::int64_t>(*fc) - static_cast<std::int64_t>(*last_frame_count);
										auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(*time - last_frame_time);
										plain_writer.Write(u8"\r\n");
										temp_output.clear();
										std::format_to(std::back_insert_iterator(temp_output), "\t\t\t\t=====[{}]Frame  [{}]ms=====\t\t\t\t", count, dur.count());
										for (std::size_t i = 0; i < 10; ++i)
										{
											plain_writer.Write(temp_output);
										}
										plain_writer.Write(u8"\r\n");
										last_frame_count = *fc;
										last_frame_time = *time;
									}
								}
								else {
									last_frame_count = fc;
									last_frame_time = *time;
								}
							}
						}

						if (setting.output_with_line)
						{
							temp_output.clear();
							std::format_to(std::back_insert_iterator(temp_output), "Line-{}:{}", log_line.line.Begin(), Potato::Log::AddLogStringWrapper(log_line.total_str));
							plain_writer.Write(temp_output);
						}
						else {
							plain_writer.Write(log_line.total_str);
						}

						return true;
						});
					
					if (setting.find_mode != UEBabyPram::LogFilter::FindMode::None)
					{
						std::string out_buffer;
						std::format_to(
							std::back_insert_iterator(out_buffer),
							"FileName:<{}> : ",
							file_path.generic_string()
						);
						for (auto& ite : line_record)
						{
							std::format_to(
								std::back_insert_iterator(out_buffer),
								"{},",
								ite.string
							);
						}
						std::format_to(
							std::back_insert_iterator(out_buffer),
							"]",
							file_path.generic_string()
						);
						Log::Log<log_filter, Log::LogLevel::Display, "{}">(out_buffer);
					}
					
					Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Finish filte <{}>">(out_path.generic_u16string());
				}
				else {
					Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Unable to filte file <{}>">(file_path.generic_u16string());
				}
			}
		);
	}
	context.CreateThreads(context.GetSuggestThreadCount());
	context.ExecuteContextThreadUntilNoExistTask();

	Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"All Done">();
	

	return 0;
}