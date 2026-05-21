import UEBabyPram;
import UEBabyPramLogFilter;
import UEBabyPramLogCLI;
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

std::filesystem::path GetTemporaryPath(std::filesystem::path const& reference_path)
{
	auto temporary_path = std::filesystem::temp_directory_path();
	temporary_path += reference_path.filename();
	std::string time_file_name;
	std::format_to(std::back_insert_iterator(time_file_name), "_{}.temp", std::chrono::system_clock::now().time_since_epoch().count());
	temporary_path += time_file_name;
	return temporary_path;
}

int main(int argc, char* argv[])
{

	UEBabyPram::LogFilter::FilterSetting setting;
	UEBabyPram::LogFilter::LogFilterProcessor processor;

	UEBabyPram::LogFilter::HandleComment(argc, argv, setting, processor);

	if (setting.input_file.empty())
	{
		Log::Log<log_filter, Log::LogLevel::Error, L"Require an target file with -f or --file, see -h or --help for more infomation">();
		return -1;
	}

	std::pmr::vector<std::filesystem::path> paths = std::move(setting.input_file);

	for (auto& ite : paths)
	{
		auto find = std::find(setting.input_file.begin(), setting.input_file.end(), ite);
		if (find == setting.input_file.end())
		{
			setting.input_file.emplace_back(std::move(ite));
		}
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
						auto temporary_path = GetTemporaryPath(file_path);
						std::filesystem::copy_file(
							file_path,
							temporary_path,
							std::filesystem::copy_options::overwrite_existing
						);
						reader.Open(temporary_path);
					}
				}

				if (reader)
				{
					auto temporary_output_path = GetTemporaryPath(file_path);
					temporary_output_path += ".output";
					std::filesystem::path out_path = setting.output_path;
					if (out_path.empty())
					{
						out_path = file_path;
					}
					else {
						out_path += file_path.filename();
					}

					if (!setting.output_expand.empty())
					{
						out_path += setting.output_expand;
					}

					Potato::Document::PlainTextReader::Config config;
					config.cache_buffer_size = std::min(static_cast<std::size_t>(1024) * 1024 * 20, reader.GetStreamSize());
					Potato::Document::PlainTextReader plain_reader(reader, config);
					Potato::Document::DocumentWriter writter;
					if (setting.find_mode == UEBabyPram::LogFilter::FindMode::None)
					{
						writter.Open(temporary_output_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
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
							"File:<{}> : ",
							file_path.generic_string()
						);
						for (std::size_t index = 0; index < line_record.size(); ++index)
						{
							auto& ite = line_record[index];
							if (index + 1 < line_record.size())
							{
								std::format_to(
									std::back_insert_iterator(out_buffer),
									"{},",
									ite.string
								);
							}
							else {
								std::format_to(
									std::back_insert_iterator(out_buffer),
									"{}",
									ite.string
								);
							}
						}
						Log::Log<log_filter, Log::LogLevel::Display, "{};">(out_buffer);
					}

					reader.Close();
					plain_writer.Flush();
					writter.Close();

					if (setting.find_mode == UEBabyPram::LogFilter::FindMode::None)
					{
						if (std::filesystem::exists(out_path))
						{
							std::filesystem::remove(out_path);
						}
						std::filesystem::rename(
							temporary_output_path,
							out_path
						);
						writter.Open(temporary_output_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
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