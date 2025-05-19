import UEBabyPramLogFilter;
import std;
import Potato;

std::wstring_view Source = 
LR"(LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
[2021.10.11-11.53.12:082][178]LogConfig: Setting CVar [[con.DebugEarlyDefault:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
)";


int main()
{

	UEBabyPram::LogFilter::ForeachLogLine(Source, [](UEBabyPram::LogFilter::LogLine line){
		auto frame_count = line.GetFrameCount();
		auto time = line.GetTimePoint();

		if (time.has_value())
		{

			auto zone_t = std::chrono::zoned_time{
				std::chrono::current_zone(),
				*time
			};

			std::wstring ptr;

			std::format_to(
				std::back_inserter(ptr),
				L"{:%Y.%m.%d-%H:%M:%S}",
				zone_t
			);

			auto dif = std::chrono::duration_cast<std::chrono::years>(std::chrono::system_clock::now() - *time);
		}
	});

	std::filesystem::path filter = LR"(C:\Users\chips\Desktop\bat.txt)";

	Potato::Document::BinaryStreamReader reader(filter);
	if (reader)
	{
		auto total_size = reader.GetStreamSize();
		std::vector<std::byte> buffer;
		buffer.resize(total_size);
		reader.Read(std::span(buffer));

		Potato::Document::DocumentReader doc_reader(buffer);

		std::wstring str;

		doc_reader.Read(std::back_inserter(str));

		auto new_file = filter;
		new_file.replace_extension(L"output");
		Potato::Document::BinaryStreamWriter writter(new_file, Potato::Document::BinaryStreamWriter::OpenMode::CREATE_OR_EMPTY);
		if (writter)
		{
			std::wstring output;
			
			UEBabyPram::LogFilter::ForeachLogLine(str, [&](UEBabyPram::LogFilter::LogLine line) {
				if (!line.time.empty())
				{
					auto time = line.GetTimePoint();
					volatile int i = 0;
				}

				for (std::size_t i = 0; i < 10; ++i)
				{
					std::format_to(
						std::back_inserter(output),
						L"Line-{}:{}",
						line.line.Begin(),
						line.total_str
					);
				}
				});
			std::format_to(std::back_inserter(output), L"\r\nEOF {}", L"EndOfFile");

			Potato::Document::DocumentWriter doc_writer(Potato::Document::BomT::UTF8, true);
			doc_writer.Write(output);
			doc_writer.FlushTo(writter);
		}
	}

	return 0;
}