module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{
	void ParserThreadTimeLine::AppendBeginEvent(double start_time, std::uint32_t event_id)
	{
		stacks.emplace_back(
			EventID{ event_id },
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
			EventID{},
			end_time,
			depth
		);
		if (depth == 0)
		{
			reference.OnCPUStackTree(ThreadCPUEventView{
				thread_id,
				thread_system_id,
				std::span(stacks.data(), stacks.size())
				});
			stacks.clear();
		}
	}

	ParserThreadTimeLine::~ParserThreadTimeLine()
	{
		//assert(depth == 0);
	}

	auto ThreadCPUEventView::FindNextEvent(std::span<EventID const> event_id_span, EventIterator current, bool need_depper) const ->EventIterator
	{
		std::size_t iterator_index = 0;
		if (current)
		{
			if (need_depper)
			{
				iterator_index = current.exist_range.Begin() + 1;
			}
			else {
				iterator_index = current.exist_range.End();
			}
		}

		EventIterator result;
		double child_time_total = 0.0;
		double child_time_start = 0.0;
		for (; iterator_index < view.size(); ++iterator_index)
		{
			auto& ref = view[iterator_index];
			if (ref.event_id)
			{
				if (
					!result.event_id
					&& (
						event_id_span.size() == 0
						|| std::find(event_id_span.begin(), event_id_span.end(), ref.event_id) != event_id_span.end()
						)
					)
				{
					result.exist_range.StartPoint = iterator_index;
					result.exist_range.EndPoint = iterator_index;
					result.exist_time_in_second.StartPoint = ref.time_as_second;
					result.exist_time_in_second.EndPoint = ref.time_as_second;
					result.depth = ref.depth;
					result.event_id = ref.event_id;
				}
				else if (
					result.event_id
					&& ref.depth == result.depth + 1 
					)
				{
					child_time_start = ref.time_as_second;
				}
			}
			else if (result.event_id)
			{
				if (result.depth == ref.depth)
				{
					result.exist_range.EndPoint = iterator_index + 1;
					result.exist_time_in_second.EndPoint = ref.time_as_second;
					break;
				}
				else if (ref.depth == result.depth + 1)
				{
					child_time_total += ref.time_as_second - child_time_start;
					child_time_start = 0.0;
				}
			}
		}
		if (result.event_id)
		{
			result.self_time_in_second = result.exist_time_in_second.Size() - child_time_total;
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
			return timeline.thread_id.id == thread_id;
			});
		if (ite != thread_timelines.end())
		{
			if (ite->time_line)
				return ite->time_line.get();
			ite->time_line = std::unique_ptr<ParserThreadTimeLine>(new ParserThreadTimeLine{ ite->thread_id, ite->thread_system_id, *this });
			if (ite->time_line)
				return ite->time_line.get();
		}

		return nullptr;
	}

	/*
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
	*/

	uint32 ParserInterface::OnCPUEventDiscoverd(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line)
	{
		auto cur_event_name = CoverStringView(event_name, event_name_len);
		auto cur_file_name = CoverStringView(file, file_name_len);
		auto event_id = time_infos.size();
		time_infos.emplace_back(
			EventID{ event_id },
			std::wstring{ cur_event_name },
			std::wstring{ cur_file_name },
			line
		);
		if (cur_event_name == L"Frame")
		{
			frame_event_id.push_back(event_id);
		}
		OnCPUEventDiscoverd(EventID{ event_id }, cur_event_name, cur_file_name, line);
		return static_cast<uint32>(event_id);
	}

	auto ParserInterface::GetCPUEventInfo(EventID event_id) const -> std::optional<ParserInterface::CPUEventInfo>
	{
		if (event_id.id < time_infos.size())
		{
			auto& ref = time_infos[event_id.id];
			return CPUEventInfo{ ref.id, ref.event_name, ref.file_name, ref.file_line };
		}
		return std::nullopt;
	}

	
	auto ParserInterface::GetThreadInfo(ThreadID thread_id) const -> std::optional<ThreadInfo>
	{
		auto find = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_id == thread_id;
			});
		if (find != thread_timelines.end())
		{
			return ThreadInfo{ find->thread_id, find->thread_system_id, find->thread_name };
		}
		return std::nullopt;
	}

	auto ParserInterface::GetThreadInfo(ThreadSystemID thread_id) const->std::optional<ThreadInfo>
	{
		auto find = std::find_if(thread_timelines.begin(), thread_timelines.end(), [thread_id](const auto& timeline) {
			return timeline.thread_system_id == thread_id;
			});
		if (find != thread_timelines.end())
		{
			return ThreadInfo{ find->thread_id, find->thread_system_id, find->thread_name };
		}
		return std::nullopt;
	}

	void ParserInterface::SetMetadata(uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId)
	{
		AddMetaData(TimerId, format, meta_data, meta_data_len, ThreadId);
	}

	uint32 ParserInterface::AddMetaData(uint32 event_id, MetaDataFormat format, uint8 const* data, std::size_t meta_data_len, uint32 thread_id)
	{
		return 0;
		/*
		if (thread_id == 1)
		{
			volatile int ui = 0;
		}
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
		*/
	}

	void ParserInterface::OnThreadDiscoverd(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len)
	{
		std::string_view thread_name_view{ thread_name, thread_name_len };
		thread_timelines.emplace_back(
			ThreadID{ thread_id },
			ThreadSystemID{ thread_system_id },
			std::string{ thread_name, thread_name_len },
			std::unique_ptr<ParserThreadTimeLine>{}
		);
		OnThreadDiscoverd(ThreadID{ thread_id }, ThreadSystemID{ thread_system_id }, thread_name_view);
	}

	void ParserInterface::AllAnalyzeDone()
	{
		for (auto& ite : thread_timelines)
		{
			if (ite.time_line)
			{
				while (ite.time_line->depth != 0 && ite.time_line->stacks.size() > 0)
				{
					ite.time_line->AppendEndEvent(
						ite.time_line->stacks.rbegin()->time_as_second
					);
				}
			}
		}
	}
}