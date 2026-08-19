module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{
	void ParserThreadTimeLine::AppendBeginEvent(double start_time, std::uint32_t event_id)
	{
		stacks.emplace_back(
			event_id,
			start_time,
			depth
		);
		++depth;
	}

	void ParserThreadTimeLine::AppendEndEvent(double end_time)
	{
		assert(depth > 0);
		if (depth > 0)
		{
			--depth;
		}
		stacks.emplace_back(
			std::nullopt,
			end_time,
			depth
		);
		if (depth == 0)
		{
			reference.OnCPUStackTree(ThreadCPUEventView{
				thread_id,
				std::span(stacks.data(), stacks.size()),
				frame_count
				});
			stacks.clear();
			frame_count.reset();
		}
	}

	ParserThreadTimeLine::~ParserThreadTimeLine()
	{
		//assert(depth == 0);
	}

	auto ThreadCPUEventView::FindNextEvent(std::size_t event_id, EventIterator current, IteratorMode mode) const ->EventIterator
	{
		std::size_t iterator_index = 0;
		if (current)
		{
			switch (mode)
			{
			case IteratorMode::Deeper:
				iterator_index = current.exist_range.Begin() + 1;
				break;
			case IteratorMode::Shallower:
				iterator_index = current.exist_range.End() + 1;
				break;
			default:
				iterator_index = view.size();
				break;
			}
		}

		EventIterator result;
		for (; iterator_index < view.size(); ++iterator_index)
		{
			auto& ref = view[iterator_index];
			if (ref.event_id.has_value())
			{
				if (*ref.event_id == event_id)
				{
					result.exist_range.StartPoint = iterator_index;
					result.exist_range.EndPoint = iterator_index;
					result.exist_time_in_second.StartPoint = ref.time_as_second;
					result.exist_time_in_second.EndPoint = ref.time_as_second;
					result.depth = ref.depth;
				}
				else if (
					result.exist_range.Begin() != 0
					&& result.depth == ref.depth
					)
				{
					result.exist_range.EndPoint = iterator_index;
					result.exist_time_in_second.EndPoint = ref.time_as_second;
					break;
				}
			}
		}
		return result;
	}

	std::wstring_view ParserInterface::CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen)
	{
		if (ScopeName != nullptr && ScopeNameLen > 0)
		{
			if (ScopeName[ScopeNameLen - 1] == 0)
			{
				return std::wstring_view{ ScopeName, ScopeNameLen - 1 };
			}
			return std::wstring_view{ ScopeName, ScopeNameLen };
		}
		return {};
	}

	ParserThreadTimeLine* ParserInterface::GetThreadTimeLine(uint32 thread_id)
	{
		auto ite = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (ite != thread_timelines.end())
			return ite->time_line.get();
		auto new_timeline = std::unique_ptr<ParserThreadTimeLine>(new ParserThreadTimeLine{ thread_id, *this });
		if (new_timeline)
		{
			auto pointer = new_timeline.get();
			thread_timelines.emplace_back(TimeLineTuple{ thread_id, {}, std::move(new_timeline) });
			return pointer;
		}
		return nullptr;
	}

	void ParserInterface::AddThread(uint32 thread_id, char const* thread_name)
	{
		std::string_view thread_name_str;
		if (thread_name != nullptr)
		{
			thread_name_str = std::string_view(thread_name);
		}
		auto new_timeline = std::unique_ptr<ParserThreadTimeLine>(new ParserThreadTimeLine{ thread_id, *this });
		auto ite = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (ite != thread_timelines.end())
		{
			ite->thread_name = thread_name_str;
		}
		else {
			thread_timelines.emplace_back(TimeLineTuple{ thread_id, std::string{thread_name_str}, std::move(new_timeline) });
		}
		OnThreadDiscoverd(thread_id, thread_name_str);
	}

	uint32 ParserInterface::OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line)
	{
		auto cur_event_name = CoverStringView(event_name, event_name_len);
		auto cur_file_name = CoverStringView(file, file_name_len);
		auto event_id = time_infos.size();
		time_infos.emplace_back(
			event_id,
			std::wstring{ cur_event_name },
			std::wstring{ cur_file_name },
			line
		);
		if (cur_event_name == L"Frame")
		{
			frame_event_id.push_back(event_id);
		}
		OnCPUEventDiscoverd(event_id, cur_event_name, cur_file_name, line);
		return static_cast<uint32>(event_id);
	}

	std::optional<std::wstring_view> ParserInterface::GetCPUEventName(std::size_t event_id) const
	{
		if (event_id < time_infos.size())
		{
			return std::wstring_view{ time_infos[event_id].event_name };
		}
		return std::nullopt;
	}

	std::optional<std::string_view> ParserInterface::GetThreadName(std::size_t thread_id) const
	{
		auto find = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (find != thread_timelines.end())
		{
			return std::string_view{ find->thread_name };
		}
		return std::nullopt;
	}

	uint32 ParserInterface::AddMetaData(uint32 event_id, MetaDataFormat format, uint8 const* data, std::size_t meta_data_len, uint32 thread_id)
	{
		if (data != nullptr && meta_data_len != 0)
		{
			if (std::find(frame_event_id.begin(), frame_event_id.end(), event_id) != frame_event_id.end())
			{
				wchar_t const* frame_count = nullptr;
				std::size_t frame_count_len = 0;
				if (BaseParser::TryReadFromMetaData(format, data, meta_data_len, "Name", frame_count, frame_count_len) && frame_count != nullptr)
				{
					std::wstring_view frame_count_string = { frame_count, frame_count_len };
					std::size_t frame_count_num = 0;
					auto info = Potato::Format::DirectDeformat(frame_count_string, frame_count_num);
					if (info)
					{
						auto find = std::find_if(
							thread_timelines.begin(),
							thread_timelines.end(),
							[=](TimeLineTuple const& tuple) {
								return tuple.thread_id == thread_id;
							}
						);
						if (find != thread_timelines.end())
						{
							find->time_line->frame_count = frame_count_num;
						}
					}
				}
			}
		}
		return event_id;
	}

	auto ParserInterface::GetCPUEventInfo(std::size_t event_id) const ->std::optional<CPUEventInfo>
	{
		if (time_infos.size() > event_id)
		{
			auto& ref = time_infos[event_id];
			return CPUEventInfo{
				ref.id,
				ref.event_name,
				ref.file_name,
				ref.file_line
			};
		}
		return std::nullopt;
	}

	auto ParserInterface::GetThreadInfo(std::size_t thread_id) const ->std::optional<ThreadInfo>
	{
		auto find = std::find_if(thread_timelines.begin(), thread_timelines.end(), [=](TimeLineTuple const& ref) {
			return ref.thread_id == thread_id;
			});
		if (find != thread_timelines.end())
		{
			return ThreadInfo{
				find->thread_name
			};
		}
		return std::nullopt;
	}
}