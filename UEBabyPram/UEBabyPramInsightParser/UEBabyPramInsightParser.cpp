module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{

	DisplayDuration::DisplayDuration(DurationSec duration)
	{
		minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
		seconds = duration - minutes;
	}

	void ParserThreadTimeLine::AppendBeginEvent(double start_time, std::uint32_t event_id)
	{
		auto start_time_duration = DurationSec{ start_time };
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
		DurationSec current_time = DurationSec{ end_time };
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
			last_time = DurationSec::zero();
		}
	}

	ParserThreadTimeLine::~ParserThreadTimeLine()
	{
		assert(depth == 0);
	}

	auto EventSpecManager::GetEventSpec(EventID id) const ->EventSpecView
	{
		if (id)
		{
			if (id.id < max_space_count)
			{
				if (id.id < events.size())
				{
					auto& ref = events[id.id];
					EventSpecView view;
					view.id = ref.id;
					view.line = ref.line;
					view.name_view = ref.name_index.Slice(std::wstring_view{ string_storage });
					view.file_view = ref.file_index.Slice(std::wstring_view{ string_storage });
					return view;
				}
			}
			else {
				auto find = out_range_spec.find(id);
				if (find != out_range_spec.end())
				{
					EventSpecView view;
					view.id = find->second.id;
					view.line = find->second.line;
					view.name_view = find->second.name_index.Slice(std::wstring_view{ string_storage });
					view.file_view = find->second.file_index.Slice(std::wstring_view{ string_storage });
					return view;
				}
			}
		}
		return {};
	}

	bool EventSpecManager::AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line)
	{
		if (id)
		{
			auto old_string_index = string_storage.size();
			auto old_data = string_storage.data();
			if (id.id < max_space_count)
			{
				if (events.size() < id.id + 1)
				{
					events.resize(id.id + 1);
				}
				auto& ref = events[id.id];
				if (ref.id)
				{
					return false;
				}
				else {
					string_storage.append(event_name);
					auto new_string_index = string_storage.size();
					if (need_file)
					{
						string_storage.append(file);
						ref.line = line;
					}
					auto new_file_index = string_storage.size();
					ref.id = id;
					ref.name_index = { old_string_index, new_string_index };
					ref.file_index = { new_string_index, new_file_index };
					ref.debug_name_view = ref.name_index.Slice(std::wstring_view{ string_storage });
					ref.debug_file = ref.file_index.Slice(std::wstring_view{ string_storage });
				}
			}
			else {
				Spec spec;
				spec.id = id;
				spec.line = line;
				auto re = out_range_spec.insert(std::pair(id, spec));
				if (re.second)
				{
					string_storage.append(event_name);
					auto new_string_index = string_storage.size();
					if (need_file)
					{
						string_storage.append(file);
						re.first->second.line = line;
					}
					auto new_file_index = string_storage.size();
					re.first->second.name_index = { old_string_index, new_string_index };
					re.first->second.file_index = { new_string_index, new_file_index };
					re.first->second.debug_name_view = re.first->second.name_index.Slice(std::wstring_view{ string_storage });
					re.first->second.debug_file = re.first->second.file_index.Slice(std::wstring_view{ string_storage });
				}
			}
			auto new_data = string_storage.data();
			if (old_data != new_data)
			{
				for (auto& ite : events)
				{
					if (ite.id)
					{
						ite.debug_file = ite.file_index.Slice(std::wstring_view{ string_storage });
						ite.debug_name_view = ite.name_index.Slice(std::wstring_view{ string_storage });
					}
				}

				for (auto& ite : out_range_spec)
				{
					if (ite.first)
					{
						ite.second.debug_file = ite.second.file_index.Slice(std::wstring_view{ string_storage });
						ite.second.debug_name_view = ite.second.name_index.Slice(std::wstring_view{ string_storage });
					}
				}
			}
		}
		return false;
	}

	void ContextSwitchEventList::AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time)
	{
		auto find = thread_list.find(thread_id);
		if (find == thread_list.end())
		{
			ThreadList list{thread_id, fast_check_point_count, &resource};
			list.thread_system_id = thread_id;
			find = std::get<0>(thread_list.insert(std::pair(thread_id, std::move(list))));
		}
		find->second.events.emplace_back(active_core, active_time);
		if ((find->second.events.size() % fast_check_point_count) == 0)
		{
			find->second.fast_check_point.emplace_back(active_time.Begin());
		}
	}

	std::size_t ContextSwitchEventList::ThreadList::FastLocateFirstEventIndex(DurationSec target_point) const
	{
		std::size_t start_index = 0;
		for (; start_index < fast_check_point.size(); ++start_index)
		{
			if (fast_check_point[start_index] > target_point)
				break;
		}
		return start_index * fast_check_point_count;
	}
	std::size_t ContextSwitchEventList::ThreadList::LocateFirstEventIndex(DurationSec target_point, std::size_t index_offset) const
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

	auto ContextSwitchEventList::ThreadList::GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const ->Statis
	{
		auto fast_start = FastLocateFirstEventIndex(time_range.Begin());
		auto start = LocateFirstEventIndex(time_range.Begin(), fast_start);
		auto end = LocateFirstEventIndex(time_range.End(), start);

		std::size_t active_count = 0;
		DurationSec active_time = DurationSec::zero();

		DurationSec overlapping_time = DurationSec::zero();

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
			overlapping_time += end_time_range.End() - time_range.End();
		}

		auto total_duration = DurationSec::zero();

		for (auto i : Potato::Misc::IndexSpan<>(start, end + 1))
		{
			total_duration += events[i].active_time_range.Size();
		}

		total_duration -= overlapping_time;

		assert(total_duration.count() >= 0.0);

		return Statis{
			total_duration,
			active_count
		};

	}

	auto ContextSwitchEventList::GetContextSwitchStatis(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationSec> time_range) const ->Statis
	{
		auto find = thread_list.find(thread_id);
		if (find != thread_list.end())
		{
			return find->second.GetContextSwitchStatis(time_range);
		}
		return {};
	}

	auto ThreadCPUEventView::FindFirstEventID(std::span<EventID const> event_ids, Potato::Misc::IndexSpan<> index_range) const -> FindResult
	{
		std::size_t end = std::min(index_range.End(), view.size());
		for (std::size_t index = index_range.Begin(); index < end; ++index)
		{
			auto& ref = view[index];
			if (std::find(event_ids.begin(), event_ids.end(), ref.event_id) != event_ids.end() || (event_ids.empty() && ref.event_id))
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

	auto ThreadCPUEventView::GetExcludeTime(EventView view) const -> std::optional<ExcludeTimeStatis>
	{
		if (view)
		{
			DurationSec start_point = view.time_range.Begin();

			DurationSec exclude_time = DurationSec::zero();

			std::size_t offset = view.index_range.Begin() + 1;
			while (true)
			{
				auto child_event = FindNextEvent({}, { offset, view.index_range.End() });

				if (!child_event)
				{
					assert(start_point <= view.time_range.End());
					exclude_time +=  Potato::Misc::IndexSpan<DurationSec>{ start_point, view.time_range.End() }.Size();
					return ExcludeTimeStatis{ exclude_time, exclude_time, 1 };
				}

				assert(start_point <= child_event.time_range.Begin());

				exclude_time += Potato::Misc::IndexSpan<DurationSec>{ start_point, child_event.time_range.Begin() }.Size();

				start_point = child_event.time_range.End();

				offset = child_event.index_range.End();
			}
		}
		return std::nullopt;
	}

	auto ThreadCPUEventView::GetExcludeTime(EventView view, ContextSwitchEventList const& list) const -> std::optional<ExcludeTimeStatis>
	{
		if (view)
		{
			DurationSec start_point = view.time_range.Begin();

			DurationSec exclude_time = DurationSec::zero();
			DurationSec exclude_time_remove_context_switch = DurationSec::zero();
			std::size_t context_switch_count = 0;

			std::size_t offset = view.index_range.Begin() + 1;
			while (true)
			{
				auto child_event = FindNextEvent({}, { offset, view.index_range.End() });

				if (!child_event)
				{
					assert(start_point <= view.time_range.End());
					exclude_time += Potato::Misc::IndexSpan<DurationSec>{ start_point, view.time_range.End() }.Size();
					auto statis = list.GetContextSwitchStatis(system_thread_id, { start_point, view.time_range.End() });
					assert(statis.active_time.count() >= 0.0);
					exclude_time_remove_context_switch += statis.active_time;
					context_switch_count += statis.switch_count;
					return ExcludeTimeStatis{ exclude_time, exclude_time_remove_context_switch, context_switch_count};
				}

				assert(start_point <= child_event.time_range.Begin());
				exclude_time += Potato::Misc::IndexSpan<DurationSec>{ start_point, child_event.time_range.Begin() }.Size();
				auto statis = list.GetContextSwitchStatis(system_thread_id, { start_point, child_event.time_range.Begin() });
				assert(statis.active_time.count() >= 0.0);
				exclude_time_remove_context_switch += statis.active_time;
				context_switch_count += statis.switch_count;

				start_point = child_event.time_range.End();

				offset = child_event.index_range.End();
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