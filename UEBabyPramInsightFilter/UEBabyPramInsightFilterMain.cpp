
import Potato;
import std;
import UEBabyPramInsightParser;
import UEBabyPramInsightFilterGameStatic;

using UEBabyPram::InsightParser::ThreadID;
using UEBabyPram::InsightParser::ThreadSystemID;
using UEBabyPram::InsightParser::EventID;

using namespace UEBabyPram::InsightFilter;

int main(int argc, char* argv[])
{

	std::filesystem::path insight_path;

	for (std::size_t i = 0; i < argc; ++i)
	{
		std::string_view arg = argv[i];
		if (arg == "-f" || arg == "--file")
		{
			if (i + 1 < argc)
			{
				std::u8string path;
				Potato::Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path });
				insight_path = path;
				break;
			}
			else {
				std::cerr << "Error: No insight file path provided after " << arg << std::endl;
				return -1;
			}
		}
	}

	if (std::endian::native == std::endian::big)
	{
		return -1;
	}

	auto start_time = std::chrono::system_clock::now();

	if (!insight_path.empty() && std::filesystem::exists(insight_path))
	{
		Potato::Document::DocumentReader Reader(insight_path);
		GameThreadStatic game_thread_static;
		//Parser Ana;
		UEBabyPram::InsightParser::ExecuteParser(Reader, game_thread_static);
		
		Potato::Log::Log<"Output", Potato::Log::LogLevel::Display, 
			"Total GameThread Time: <{}s>, Total GameFram :<{}>, Avg GameThread Time: <{}s>"
		>(
			game_thread_static.total_time.count(),
			game_thread_static.total_count,
			game_thread_static.total_time.count() / game_thread_static.total_count
		);

	}

	auto end_time = std::chrono::system_clock::now();

	Potato::Log::Log<"sadasd", Potato::Log::LogLevel::Display, "Duration Time: <{}> microseconds">(
		std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
	);

	return 0;
}