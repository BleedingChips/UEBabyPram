module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{
	void ParserThreadTimeLine::AppendBeginEvent(double start_time, std::uint32_t event_id)
	{
		auto start_time_duration = DurationT{ start_time };
		stacks.emplace_back(
			EventID{ event_id },
			start_time_duration,
			depth
		);
		++depth;
		last_time = start_time_duration;
	}

	void ParserThreadTimeLine::AppendEndEvent(double end_time)
	{
		DurationT current_time = DurationT{ end_time };
		if (end_time == std::numeric_limits<double>::infinity())
		{
			current_time = last_time;
		}
		assert(depth > 0);
		if (depth > 0)
		{
			--depth;
		}
		stacks.emplace_back(
			EventID{},
			current_time,
			depth
		);
		last_time = current_time;
		if (depth == 0)
		{
			reference.OnCPUStackTree(ThreadCPUEventView{
				thread_id,
				thread_system_id,
				std::span(stacks.data(), stacks.size())
				});
			stacks.clear();
			last_time = DurationT::zero();
		}
	}

	ParserThreadTimeLine::~ParserThreadTimeLine()
	{
		//assert(depth == 0);
	}

	void ContextSwitchEventList::AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationT> active_time)
	{
		auto find = thread_list.find(thread_id);
		if (find == thread_list.end())
		{
			ThreadList list{thread_id, fast_check_point_count};
			list.thread_system_id = thread_id;
			find = std::get<0>(thread_list.insert(std::pair(thread_id, std::move(list))));
		}
		find->second.events.emplace_back(active_core, active_time);
		if ((find->second.events.size() % fast_check_point_count) == 0)
		{
			find->second.fast_check_point.emplace_back(active_time.Begin());
		}
	}

	std::size_t ContextSwitchEventList::ThreadList::FastLocateFirstEventIndex(DurationT target_point) const
	{
		std::size_t start_index = 0;
		for (; start_index < fast_check_point.size(); ++start_index)
		{
			if (fast_check_point[start_index] > target_point)
				break;
		}
		return start_index * fast_check_point_count;
	}
	std::size_t ContextSwitchEventList::ThreadList::LocateFirstEventIndex(DurationT target_point, std::size_t index_offset) const
	{
		for (; index_offset < events.size(); ++index_offset)
		{
			if (events[index_offset].active_time_range.End() > target_point)
			{
				break;
			}
		}
		return index_offset;
	}

	auto ContextSwitchEventList::ThreadList::GetContextSwitchStatic(Potato::Misc::IndexSpan<DurationT> time_range) const ->Static
	{
		auto fast_start = FastLocateFirstEventIndex(time_range.Begin());
		auto start = LocateFirstEventIndex(time_range.Begin(), fast_start);
		auto end = LocateFirstEventIndex(time_range.End(), start);

		std::size_t active_count = 0;
		DurationT active_time = DurationT::zero();

		DurationT overlapping_time = DurationT::zero();

		auto start_time = events[start].active_time_range.Begin();
		auto end_time_range = events[end].active_time_range;

		if(start_time < time_range.Begin())
			overlapping_time += time_range.Begin() - start_time;

		if (time_range.End() < end_time_range.Begin())
		{
			active_count -= 1;
			overlapping_time += end_time_range.Size();
		}
		else {
			overlapping_time += time_range.End() - end_time_range.Begin();
		}

		auto total_duration = DurationT::zero();

		for (auto i : Potato::Misc::IndexSpan<>(start, end + 1))
		{
			total_duration += events[i].active_time_range.Size();
		}

		total_duration -= overlapping_time;

		return Static{
			total_duration,
			active_count
		};

	}

	auto ContextSwitchEventList::GetContextSwitchStatic(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationT> time_range) const ->Static
	{
		auto find = thread_list.find(thread_id);
		if (find != thread_list.end())
		{
			return find->second.GetContextSwitchStatic(time_range);
		}
		return {};
	}

	auto ThreadCPUEventView::FindFirstEventID(std::span<EventID const> event_ids, Potato::Misc::IndexSpan<> index_range) const -> FindResult
	{
		std::size_t end = std::min(index_range.End(), view.size());
		for (std::size_t index = index_range.Begin(); index < end; ++index)
		{
			auto& ref = view[index];
			if (std::find(event_ids.begin(), event_ids.end(), ref.event_id) != event_ids.end())
			{
				return FindResult{ ref.event_id, index, ref.depth, ref.time };
			}
		}
		return {};
	}

	auto ThreadCPUEventView::FindFirstDepth(std::size_t depth, Potato::Misc::IndexSpan<> index_range) const -> FindResult
	{
		std::size_t end = std::min(index_range.End(), view.size());
		for (std::size_t index = index_range.Begin(); index < end; ++index)
		{
			auto& ref = view[index];
			if (depth == ref.depth)
			{
				return FindResult{ ref.event_id, index, ref.depth, ref.time };
			}
		}
		return {};
	}

	EventID ThreadCPUEventView::GetTopEvent() const {
		if (view.size() > 0)
		{
			return view[0].event_id;
		}
		return {};
	}

	auto ThreadCPUEventView::FindNextEvent(std::span<EventID const> event_id_span, Potato::Misc::IndexSpan<> index_range) const -> EventView
	{
		std::size_t edge = std::min(index_range.End(), view.size());

		auto first = FindFirstEventID(event_id_span, index_range);

		if (!first)
			return {};

		auto end = FindFirstDepth(first.depth, first.index_offset + 1);

		assert(end);

		return {
			first.event_id,
			{first.index_offset, end.index_offset + 1},
			{first.time_point, end.time_point},
			first.depth
		};
	}

	std::optional<DurationT> ThreadCPUEventView::GetExcludeTime(EventView view) const
	{
		if (view)
		{
			auto child_durations = DurationT::zero();
			auto target_depth = view.depth + 1;
			std::size_t offset = view.index_range.Begin() + 1;
			while (true)
			{
				auto next_depth = FindFirstDepth(target_depth, offset);
				if (!next_depth)
					return view.time_range.Size() - child_durations;
				auto next_end_depth = FindFirstDepth(target_depth, next_depth.index_offset + 1);
				assert(next_end_depth);
				child_durations += next_end_depth.time_point - next_depth.time_point;
				offset = next_end_depth.index_offset + 1;
			}
		}
		return std::nullopt;
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

	/*
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
	*/

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

	/*
	void ParserInterface::SetMetadata(uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId)
	{
		AddMetaData(TimerId, format, meta_data, meta_data_len, ThreadId);
	}


	uint32 ParserInterface::AddMetaData(uint32 event_id, MetaDataFormat format, uint8 const* data, std::size_t meta_data_len, uint32 thread_id)
	{
		return 0;

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
	}
	*/

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
						ite.time_line->last_time.count()
					);
				}
			}
		}
	}
}