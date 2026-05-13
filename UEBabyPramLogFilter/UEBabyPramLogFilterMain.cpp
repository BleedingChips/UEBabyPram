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
			if constexpr (level != Potato::Log::LogLevel::Display)
			{
				FormatedSystemTime time;
				iterator = std::format_to(
					std::move(iterator),
					L"[{}]{}<{}>:",
					time, log_filter, level
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
				setting.input_files.emplace_back(argv[i+1]);
			}
			else {
				return -1;
			}
		}
		else if (argv_string == "-p" || argv_string == "-path")
		{
			if (i + 1 < argc)
			{
				setting.input_paths.emplace_back(argv[i + 1]);
			}
			else {
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
			}
			else {
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
	}

	std::pmr::vector<std::filesystem::path> files;

	for (auto& ite : setting.input_files)
	{
		if (std::filesystem::exists(ite) && ite.extension() == u8".log")
		{
			files.emplace_back(ite);
		}
	}

	for (auto& ite : setting.input_paths)
	{
		for (auto const& path_ite  : std::filesystem::directory_iterator(ite))
		{
			if (path_ite.path().extension() == u8".log")
			{
				files.emplace_back(path_ite);
			}
		}
	}

	if (files.empty())
	{
		return -1;
	}

	Potato::Task::Context task_context;
	task_context.CreateThreads(std::thread::hardware_concurrency() -  1);
	struct FilterTaskContext
	{
		std::mutex mutex;
		std::filesystem::path path;
		std::filesystem::path output_path;
		bool done = false;
		std::size_t line_count = 0;
		std::size_t index = 0;
	};

	std::pmr::vector<std::shared_ptr<FilterTaskContext>> context;

	std::size_t index = 0;
	for (auto& file_ite : files)
	{
		auto new_context = std::make_shared<FilterTaskContext>();
		new_context->path = file_ite;
		new_context->index = index++;
		new_context->output_path = file_ite;
		if (setting.output_expand.empty())
		{
			new_context->output_path += u8".filter_out";
		}
		else {
			new_context->output_path += setting.output_expand;
		}
		context.emplace_back(new_context);
		Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Start Filter file:<{}>">(file_ite.generic_u8string());
		task_context.Commit([new_context, file_ite, &setting, processor](Potato::Task::Context&, Potato::Task::Node::Parameter&, Potato::Task::Node&) mutable {
			Potato::Document::DocumentReader reader(file_ite);
			if (!reader)
			{
				if (std::filesystem::exists(file_ite))
				{
					auto temporary_path = std::filesystem::temp_directory_path();
					temporary_path += file_ite.filename();
					std::filesystem::copy_file(
						file_ite,
						temporary_path
					);
					reader.Open(temporary_path);
				}
			}
			if (reader)
			{
				Potato::Document::PlainTextReader::Config config;
				config.cache_buffer_size = std::min(static_cast<std::size_t>(1024) * 1024 * 200, reader.GetStreamSize());
				Potato::Document::PlainTextReader plain_reader(reader, config);
				Potato::Document::DocumentWriter writter(new_context->output_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
				Potato::Document::PlainTextWritter::Config writer_config;
				config.bom = Potato::Document::BomT::UTF8;
				Potato::Document::PlainTextWritter plain_writer(writter, writer_config);

				UEBabyPram::LogParser::LogLine::TimeT last_frame_time;
				std::optional<std::size_t> last_frame_count;
				std::string temp_output;
				UEBabyPram::LogParser::ForeachLogLine(plain_reader, [&](UEBabyPram::LogParser::LogLine log_line) -> bool {
					
					if (
						(log_line.line.Begin() % 100) == 0
						|| log_line.line.Begin() / 100 != log_line.line.End() / 100
						)
					{
						std::lock_guard lg(new_context->mutex);
						new_context->line_count = log_line.line.Begin();
					}

					if (processor)
					{
						auto re = processor.Detect(log_line);

						if (!re.has_value() || !*re)
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
				{
					std::lock_guard lg(new_context->mutex);
					new_context->done = true;
				}
			}
		});
	}

	bool all_finish = false;
	std::size_t log_count = 0;
	while (!all_finish)
	{
		log_count++;
		all_finish = true;
		bool has_print_log = false;
		for (auto& ite : context)
		{
			std::size_t line_count;
			bool done = false;
			{
				std::lock_guard lg(ite->mutex);
				if (!ite->done)
				{
					all_finish = false;
				}
				line_count = ite->line_count;
				done = ite->done;
			}

			if (!done && (log_count % 100) == 0)
			{
				Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Filter State {}:<{}>">(ite->path.generic_u8string(), line_count);
				has_print_log = true;
			}
		}
		if (has_print_log)
		{
			Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"====\t\t====\t\t====">();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	task_context.ExecuteContextThreadUntilNoExistTask();

	for (auto& ite : context)
	{
		Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"Finish Filter <{}> From <{}>">(ite->output_path.generic_u8string(), ite->path.generic_u8string());
	}

	Potato::Log::Log<log_filter, Potato::Log::LogLevel::Log, u8"All Done">();

	return 0;
}