
import Potato;
import std;
import UEBabyPramInsightParser;

struct Parser : public UEBabyPram::InsightParser::ParserInterface
{
	void OnCPUEventDiscoverd(std::size_t id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// 4841 5607
		if (event_name.contains(L"UCharacterMovementComponent_TickComponent"))
		{
			movement_component_tick.emplace_back(id);
		}
	}

	void OnCPUStackTree(UEBabyPram::InsightParser::ThreadCPUEventView event_scope)
	{
		if (event_scope.frame_count.has_value())
		{
			UEBabyPram::InsightParser::ThreadCPUEventView::EventIterator iterator;
			do {
				iterator = event_scope.FindNextEvent(
					std::span(movement_component_tick.data(), movement_component_tick.size()),
					iterator
				);
				if (iterator)
				{
					total_time += iterator.exist_time_in_second.Size();
					count += 1;
				}
			} while (iterator);
		}
		volatile int i = 0;
	}
	std::vector<std::size_t> movement_component_tick;
	double total_time = 0.0f;
	std::size_t count = 0;
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
		Potato::Log::Log<"sadasd", Potato::Log::LogLevel::Display, "{} {} {}">(Ana.total_time, Ana.count, Ana.total_time / Ana.count);
	}

	return 0;
}