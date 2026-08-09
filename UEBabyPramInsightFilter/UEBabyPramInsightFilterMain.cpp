
import Potato;
import std;
import UEBabyPramInsightParser;

struct Parser : public UEBabyPram::InsightParser::ParserInterface
{
	virtual void OnThreadDiscoverd(uint32 thread_id, std::string_view thread_name)
	{
		if (thread_name.contains("GameThread"))
		{
			game_play_thread_id = thread_id;
		}
	}

	virtual void OnCPUStackTree(UEBabyPram::InsightParser::ThreadCPUEventView event_scope)
	{
		volatile int i = 0;
	}

	//virtual void OnCPUScope
	std::optional<std::size_t> game_play_thread_id;
};


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

	if (!insight_path.empty() && std::filesystem::exists(insight_path))
	{
		Potato::Document::DocumentReader Reader(insight_path);
		Parser Ana;
		UEBabyPram::InsightParser::ExecuteParser(Reader, Ana);
		volatile int i = 0;
	}

	return 0;
}