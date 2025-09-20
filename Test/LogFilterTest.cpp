import UEBabyPramLogFilter;
import std;
import Potato;

std::u8string_view Source = 
u8R"(LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
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

			std::string ptr;

			/*
			std::format_to(
				std::back_inserter(ptr),
				"{:%Y.%m.%d-%H:%M:%S}",
				zone_t
			);
			*/

			auto dif = std::chrono::duration_cast<std::chrono::years>(std::chrono::system_clock::now() - *time);
		}
	});

	std::filesystem::path filter = u8R"(C:\Users\chips\Desktop\bat.txt)";

	Potato::Document::BinaryStreamReader reader(filter);
	if (reader)
	{
		auto total_size = reader.GetStreamSize();
		std::vector<std::byte> buffer;
		buffer.resize(total_size);
		reader.Read(std::span(buffer));

		Potato::Document::DocumentReader doc_reader(buffer);

		std::u8string str;

		doc_reader.Read(std::back_inserter(str));

		auto new_file = filter;
		new_file.replace_extension("output");
		Potato::Document::BinaryStreamWriter writter(new_file, Potato::Document::BinaryStreamWriter::OpenMode::CREATE_OR_EMPTY);
		if (writter)
		{
			std::string output;
			
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
						"Line-{}:{}",
						line.line.Begin(),
						line.total_str
					);
				}
				});
			std::format_to(std::back_inserter(output), "\r\nEOF {}", "EndOfFile");

			Potato::Document::DocumentWriter doc_writer(Potato::Document::BomT::UTF8, true);
			doc_writer.Write(std::u8string_view{ reinterpret_cast<char8_t const*>(output.data()), output.size() });
			doc_writer.FlushTo(writter);
		}
	}

	return 0;
}