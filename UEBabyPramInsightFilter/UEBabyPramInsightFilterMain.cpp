
import Potato;
import std;
import UEBabyPramInsightParser;

using UEBabyPram::InsightParser::ThreadID;
using UEBabyPram::InsightParser::ThreadSystemID;
using UEBabyPram::InsightParser::EventID;

struct Parser : public UEBabyPram::InsightParser::ParserInterface
{
	void ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<double> duration) override
	{
		auto thread_info = GetThreadInfo(thread_id);
		if (thread_info.has_value())
		{
			if (thread_info->thread_name.contains("GameFrame"))
			{
				volatile int i = 0;
			}

			if (thread_info->thread_name.contains("GameThread"))
			{
				volatile int i = 0;
			}
		}
		else {
			volatile int i = 0;
		}

	}

	void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override
	{
		// 4841 5607
		if (event_name.contains(L"UCharacterMovementComponent_TickComponent"))
		{
			movement_component_tick.emplace_back(id);
		}
	}

	void OnCPUStackTree(UEBabyPram::InsightParser::ThreadCPUEventView event_scope)
	{
		event_scope.ForeachEvent(
			std::span(movement_component_tick.data(), movement_component_tick.size()),
			[this](UEBabyPram::InsightParser::ThreadCPUEventView::EventIterator const& iterator) -> bool {
				total_time += iterator.exist_time_in_second.Size();
				self_total_time += iterator.self_time_in_second;
				count += 1;
				return true;
			}
		);
	}

	std::vector<UEBabyPram::InsightParser::EventID> movement_component_tick;
	double total_time = 0.0;
	double self_total_time = 0.0;
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
		Potato::Log::Log<"sadasd", Potato::Log::LogLevel::Display, "<{}> <{}> <{}> <{}>">(
			Ana.total_time, 
			Ana.count, 
			Ana.total_time / Ana.count,
			Ana.self_total_time
		);
	}

	return 0;
}