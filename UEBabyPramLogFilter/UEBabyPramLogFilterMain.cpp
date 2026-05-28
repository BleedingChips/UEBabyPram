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
			if constexpr (level == Potato::Log::LogLevel::Error)
			{
				Potato::Log::FormatedSystemTime time;
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
	UEBabyPram::LogFilter::LogFilterFormatter formatter;

	auto resulr = UEBabyPram::LogFilter::HandleComment(argc, argv, setting, processor, formatter);
	if (resulr > 0)
		return 0;
	else if(resulr < 0)
		return resulr;

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

	if (setting.target == UEBabyPram::LogFilter::OutputTarget::STD)
	{
		forbid_log = true;
	}

	Potato::Task::Context context;

	for (auto& file_path : setting.input_file)
	{
		context.Commit(
			[&, file_path](Potato::Task::Context&, Potato::Task::Node::Parameter&, Potato::Task::Node&) {
				Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Input <{}>">(file_path.generic_u8string());
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
					if (setting.target == UEBabyPram::LogFilter::OutputTarget::FILE)
					{
						writter.Open(temporary_output_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
					}
					Potato::Document::PlainTextWritter::Config writer_config;
					config.bom = Potato::Document::BomT::UTF8;
					Potato::Document::PlainTextWritter plain_writer(writter, writer_config);

					UEBabyPram::LogParser::LogLine::TimeT last_frame_time;
					std::optional<std::size_t> last_frame_count;
					std::u8string formated_line;
					std::chrono::system_clock::time_point last_log_time = std::chrono::system_clock::now();
					struct LineCount
					{
						std::string string;
					};
					std::pmr::deque<LineCount> line_record;
					std::size_t count = 0;
					std::optional<std::size_t> min_line;
					std::size_t max_line = 0;
					UEBabyPram::LogParser::ForeachLogLine(plain_reader, [&](UEBabyPram::LogParser::LogLine log_line) -> bool {

						if (processor)
						{
							auto re = processor.Detect(log_line);

							if (!re.has_value() || !*re)
								return true;
						}

						if (!min_line.has_value())
						{
							min_line = log_line.line.Begin();
							max_line = *min_line + 1;
						}
						else {
							max_line = log_line.line.Begin() + 1;
						}

						count += 1;
						if (count >= setting.max_output_count)
							return true;

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

										std::u8string sperate;

										std::format_to(
											Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(sperate)),
											"===[{}]Frame  [{}]ms===",
											count, dur.count()
										);
										if (setting.target == UEBabyPram::LogFilter::OutputTarget::FILE)
										{

											for (std::size_t i = 0; i < 10; ++i)
											{
												formated_line += u8"\t\t\t\t";
												formated_line += sperate;
												formated_line += u8"\t\t\t\t";
											}
										}
										else {
											formated_line += sperate;
										}
										formated_line += u8"\r\n";
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

						if (setting.mode == UEBabyPram::LogFilter::OutputMode::NORMAL)
						{
							formated_line += log_line.total_str;
						}
						else if(setting.mode == UEBabyPram::LogFilter::OutputMode::NORMAL_WITH_LINE) 
						{
							std::format_to(
								Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(formated_line)),
								"line{}-{}",
								log_line.line.Begin(),
								Potato::Log::AddLogStringWrapper(log_line.total_str)
							);
						}
						else if (setting.mode == UEBabyPram::LogFilter::OutputMode::ONLY_TIME_AND_LINE)
						{
							if (!log_line.property.time.year.empty())
							{
								std::format_to(
									Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(formated_line)),
									"[Time:({}) Line:({})]\r\n",
									Potato::Log::AddLogStringWrapper(log_line.property.time.total),
									log_line.line.Begin()
								);
							}
							else {
								std::format_to(
									Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(formated_line)),
									"[Time:(---) Line:({})]\r\n",
									log_line.line.Begin()
								);
							}
						}
						else if (setting.mode == UEBabyPram::LogFilter::OutputMode::CUSTOM)
						{
							auto string = formatter.Format(log_line);
							if (string.has_value())
							{
								std::format_to(
									Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(formated_line)),
									"{}",
									Potato::Log::AddLogStringWrapper(*string)
								);
							}
						}

						if (setting.target == UEBabyPram::LogFilter::OutputTarget::FILE)
						{
							plain_writer.Write(formated_line);
							formated_line.clear();
						}

						return true;
						});

					;

					if (setting.target == UEBabyPram::LogFilter::OutputTarget::STD)
					{
						std::string out_buffer;
						std::format_to(
							std::back_insert_iterator(out_buffer),
							"Input <{}>, <{}> Logs Matched",
							file_path.generic_string(),
							count
						);
						if (min_line.has_value())
						{
							std::format_to(
								std::back_insert_iterator(out_buffer),
								", LineRange[{}, {}]",
								*min_line,
								max_line
							);
						}
						Log::Log<log_filter, Log::LogLevel::Display, "{} : {};">(out_buffer, formated_line);
					}

					reader.Close();
					plain_writer.Flush();
					writter.Close();

					if (setting.target == UEBabyPram::LogFilter::OutputTarget::FILE)
					{
						if (std::filesystem::exists(out_path))
						{
							std::filesystem::remove(out_path);
						}
						std::filesystem::rename(
							temporary_output_path,
							out_path
						);
					}
					if (min_line.has_value())
					{
						Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Output <{}>, <{}> Logs Matched, LineRange[{}, {}]">(
							out_path.generic_u16string(), count, *min_line, max_line
						);
					}
					else {
						Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Output <{}>, <{}> Logs Matched">(
							out_path.generic_u16string(), count
						);
					}
					
				}
				else {
					Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Unable To Handle File <{}>">(file_path.generic_u16string());
				}
			}
		);
	}
	context.CreateThreads(context.GetSuggestThreadCount());
	context.ExecuteContextThreadUntilNoExistTask();

	Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"All Done">();


	return 0;
}