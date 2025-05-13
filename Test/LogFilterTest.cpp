import UEBabyPramLogFilter;
import std;
import Potato;

std::wstring_view Source = 
LR"(LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Setting CVar [[con.DebugEarlyDefault:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
)";


int main()
{

	UEBabyPram::LogFilter::ForeachLogLine(Source, [](UEBabyPram::LogFilter::LogLine line){
		if (!line.time.empty())
		{
			auto time = UEBabyPram::LogFilter::LogLineProcessor::GetTime(line);
			volatile int i = 0;
		}
		volatile int i = 0;
	});

	std::filesystem::path filter = LR"(C:\Users\chips\Desktop\bat.txt)";

	Potato::Document::BinaryStreamReader reader(filter);
	if (reader)
	{
		Potato::Document::DocumentReader doc_reader(reader);
		auto new_file = filter;
		new_file.replace_extension(L"output");
		Potato::Document::BinaryStreamWriter writter(new_file, Potato::Document::BinaryStreamWriter::OpenMode::CREATE_OR_EMPTY);
		if (writter)
		{
			Potato::Document::DocumentWriter doc_writer(writter, Potato::Document::BomT::UTF8, true);
			UEBabyPram::LogFilter::ForeachLogLine(doc_reader, [&](UEBabyPram::LogFilter::LogLine line) {
				if (!line.time.empty())
				{
					auto time = UEBabyPram::LogFilter::LogLineProcessor::GetTime(line);
					volatile int i = 0;
				}
				std::format_to(
					doc_writer.AsOutputIterator(),
					L"Line-{}:{}",
					line.line.Begin(),
					line.total_str
				);
				volatile int i = 0;
				});
			std::format_to(doc_writer.AsOutputIterator(), L"\r\nEOF {}", L"EndOfFile");
		}
		
		
		
	}

	return 0;
}