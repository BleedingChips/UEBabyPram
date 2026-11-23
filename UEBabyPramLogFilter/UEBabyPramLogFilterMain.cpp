import UEBabyPram;
import UEBabyPramLogFilter;
import UEBabyPramLogCLI;
import Potato;
import std;

using namespace UEBabyPram;
using namespace UEBabyPram::LogFilter;
using namespace Potato;

namespace
{
	std::filesystem::path GetTemporaryPath(std::filesystem::path const& reference_path)
	{
		auto temporary_path = std::filesystem::temp_directory_path();
		temporary_path += reference_path.filename();
		std::string time_file_name;
		std::format_to(std::back_insert_iterator(time_file_name), "_{}.temp", std::chrono::system_clock::now().time_since_epoch().count());
		temporary_path += time_file_name;
		return temporary_path;
	}

	std::wstring ToWide(std::u8string_view text)
	{
		std::wstring wide;
		Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(text, std::back_inserter(wide));
		return wide;
	}
}

int main(int argc, char* argv[])
{
	UEBabyPram::LogFilter::FilterSetting setting;
	UEBabyPram::LogFilter::LogFilterProcessor processor;

	try {
		auto result = UEBabyPram::LogFilter::HandleComment(argc, argv, setting, processor);
		if (result > 0)
		{
			return 0;
		}
		else if (result < 0)
		{
			return result;
		}
	}
	catch (std::exception const& exc)
	{
		LogToConsole<Log::LogLevel::Error, L"{}">(std::string_view{ exc.what() });
		return -1;
	}

	std::filesystem::path const& input_file = setting.input_file;
	std::filesystem::path const& output_file = setting.output_file;
	bool const write_log = !output_file.empty();

	Potato::Document::DocumentReader reader(input_file);
	if (!reader)
	{
		if (std::filesystem::exists(input_file))
		{
			auto temporary_path = GetTemporaryPath(input_file);
			std::filesystem::copy_file(
				input_file,
				temporary_path,
				std::filesystem::copy_options::overwrite_existing
			);
			reader.Open(temporary_path);
		}
	}

	if (!reader)
	{
		LogToConsole<Log::LogLevel::Error, L"Unable to open <{}>">(input_file.generic_wstring());
		return -1;
	}

	Potato::Document::PlainTextReader::Config reader_config;
	reader_config.cache_buffer_size = std::min(static_cast<std::size_t>(1024) * 1024 * 100, reader.GetStreamSize());
	Potato::Document::PlainTextReader plain_reader(reader, reader_config);

	std::filesystem::path temporary_output_path;
	Potato::Document::DocumentWriter writer;
	if (write_log)
	{
		temporary_output_path = GetTemporaryPath(input_file);
		temporary_output_path += ".output";
		writer.Open(temporary_output_path, Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY);
		if (!writer)
		{
			LogToConsole<Log::LogLevel::Error, L"Unable to create <{}>">(output_file.generic_wstring());
			return -1;
		}
	}

	Potato::Document::PlainTextWriter::Config writer_config;
	writer_config.bom = Potato::Document::BomT::UTF8;
	Potato::Document::PlainTextWriter plain_writer(writer, writer_config);

	std::size_t matched_count = 0;
	std::size_t output_index = 0;
	std::u8string first_time;
	std::u8string last_time;
	std::size_t first_line = 0;
	std::size_t last_line = 0;
	bool have_first_time = false;
	bool have_last_time = false;
	bool have_line = false;

	UEBabyPram::LogParser::LogLine::TimeT last_frame_time;
	std::optional<std::size_t> last_frame_count;
	std::u8string formatted_line;
	std::u8string separator_line;

	UEBabyPram::LogParser::ForeachLogLine(plain_reader, [&](UEBabyPram::LogParser::LogLine log_line) -> bool {
		if (processor)
		{
			auto matched = processor.Detect(log_line);
			if (!matched.has_value() || !*matched)
			{
				return true;
			}
		}

		++matched_count;

		if (!have_line)
		{
			first_line = log_line.line.Begin();
			have_line = true;
		}
		last_line = log_line.line.Begin();

		if (!log_line.property.time.total.empty())
		{
			if (!have_first_time)
			{
				first_time.assign(log_line.property.time.total.begin(), log_line.property.time.total.end());
				have_first_time = true;
			}

			last_time.assign(log_line.property.time.total.begin(), log_line.property.time.total.end());
			have_last_time = true;
		}

		if (!write_log)
		{
			return true;
		}

		if (!setting.output_span.IsInclude(output_index))
		{
			++output_index;
			return true;
		}

		formatted_line.clear();
		separator_line.clear();

		if (setting.mode == UEBabyPram::LogFilter::OutputMode::NORMAL_WITH_LINE)
		{
			std::format_to(
				Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(formatted_line)),
				"line-{}:",
				log_line.line.Begin()
			);
		}

		if (setting.output_with_separate_frame)
		{
			auto frame_count = log_line.GetFrameCount();
			auto time = log_line.GetSystemClockTimePoint();

			if (frame_count.has_value() && time.has_value())
			{
				if (last_frame_count.has_value())
				{
					if (*frame_count != *last_frame_count)
					{
						std::int64_t frame_delta = static_cast<std::int64_t>(*frame_count) - static_cast<std::int64_t>(*last_frame_count);
						auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(*time - last_frame_time);

						std::u8string separator_block;
						std::format_to(
							Potato::Encode::FormatterOutputIteratorWrapper(std::back_insert_iterator(separator_block)),
							"===[{}]Frame  [{}]ms===",
							frame_delta,
							duration.count()
						);

						// The frame separator is a single ruler line: the block is repeated ten times, and every
						// repetition is preceded by a tab, so the line stands out from the log text around it.
						for (std::size_t repeat = 0; repeat < 10; ++repeat)
						{
							separator_line += u8"\t\t\t\t";
							separator_line += separator_block;
							separator_line += u8"\t\t\t\t";
						}
						separator_line += u8"\r\n";

						last_frame_count = *frame_count;
						last_frame_time = *time;
					}
				}
				else {
					last_frame_count = frame_count;
					last_frame_time = *time;
				}
			}
		}

		if (!separator_line.empty())
		{
			formatted_line.insert(0, separator_line);
		}

		formatted_line += log_line.total_str;
		plain_writer.Write(formatted_line);
		++output_index;
		return true;
	});

	reader.Close();
	plain_writer.Flush();
	if (write_log)
	{
		writer.Close();

		if (std::filesystem::exists(output_file))
		{
			std::filesystem::remove(output_file);
		}
		std::filesystem::rename(temporary_output_path, output_file);
	}

	// The standard output carries the summary only: the target, the output when -o was given, the matched count
	// and the time of the first and of the last matched line.
	LogToConsole<Log::LogLevel::Display, L"Target: <{}>">(input_file.generic_wstring());
	if (write_log)
	{
		LogToConsole<Log::LogLevel::Display, L"Output: <{}>">(output_file.generic_wstring());
	}
	LogToConsole<Log::LogLevel::Display, L"Matched: <{}>">(matched_count);
	LogToConsole<Log::LogLevel::Display, L"Start: <{}>">(have_first_time ? ToWide(first_time) : std::wstring{ L"---" });
	LogToConsole<Log::LogLevel::Display, L"End: <{}>">(have_last_time ? ToWide(last_time) : std::wstring{ L"---" });
	LogToConsole<Log::LogLevel::Display, L"StartLine: <{}>">(have_line ? std::to_wstring(first_line) : std::wstring{ L"---" });
	LogToConsole<Log::LogLevel::Display, L"EndLine: <{}>">(have_line ? std::to_wstring(last_line) : std::wstring{ L"---" });

	return 0;
}
