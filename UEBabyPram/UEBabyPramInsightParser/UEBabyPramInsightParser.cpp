module;
#include <cassert>
module UEBabyPramInsightParser;


namespace UEBabyPram::InsightParser
{

	DisplayTimePoint::DisplayTimePoint(DurationSec duration)
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

	SubEventSpecManager::SubEventSpecManager(Config config)
		: need_file(config.need_file)
		, sequential_spec_blocks(config.resource)
		, random_spec_blocks(config.resource)
		, string_blocks(config.resource)
		, resource(config.resource)
	{
	}

	SubEventSpecManager::~SubEventSpecManager()
	{
		// The blocks are owned by the manager, the storage of a block is freed to the resource which it was
		// allocated from.
		for (auto block : string_blocks)
		{
			resource->deallocate(block, sizeof(StringBlock), alignof(StringBlock));
		}

		for (auto block : sequential_spec_blocks)
		{
			resource->deallocate(block, sizeof(SpecBlock), alignof(SpecBlock));
		}

		for (auto& block : random_spec_blocks)
		{
			resource->deallocate(block.block, sizeof(SpecBlock), alignof(SpecBlock));
		}
	}

	auto SubEventSpecManager::StoreString(std::u8string_view string) -> std::u8string_view
	{
		if (string.empty())
		{
			return {};
		}

		// The characters of the string are UTF-8 already, so they are written into the blocks as they are.
		return WriteString(string);
	}

	auto SubEventSpecManager::StoreString(std::wstring_view string) -> std::u8string_view
	{
		if (string.empty())
		{
			return {};
		}

		// The wide characters are encoded into UTF-8 by a local buffer, and the UTF-8 string is stored by the rule of
		// the string which is UTF-8 already.
		std::array<char8_t, 1024> encoded_buffer;
		auto info = Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(
			std::span<wchar_t const>{ string.data(), string.size() },
			std::span<char8_t>{ encoded_buffer }
		);

		// The encoding stops when the buffer is full, so the characters which are encoded before that are stored, and
		// the characters of the string which do not fit into the buffer are cut off.
		return StoreString(std::u8string_view{ encoded_buffer.data(), info.target_space });
	}

	auto SubEventSpecManager::WriteString(std::u8string_view string) -> std::u8string_view
	{
		// The characters are written after the characters which are already written into the block which is being
		// written, when the room of the block is enough for them.
		if (current_block != nullptr)
		{
			auto room = string_block_capacity - current_block_count;
			if (string.size() <= room)
			{
				auto offset = current_block_count;
				std::copy(string.begin(), string.end(), current_block->characters.data() + offset);
				current_block_count += string.size();
				return std::u8string_view{ current_block->characters.data() + offset, string.size() };
			}
		}

		// The block is allocated for the string, and the block becomes the block which is being written. The
		// characters of a string are always a slice of one block, so the characters which do not fit into the new
		// block are cut off.
		auto block = static_cast<StringBlock*>(resource->allocate(sizeof(StringBlock), alignof(StringBlock)));
		std::construct_at(block);
		string_blocks.push_back(block);
		current_block = block;
		current_block_count = 0;

		auto size = std::min(string.size(), string_block_capacity);
		std::copy(string.begin(), string.begin() + size, block->characters.data());
		current_block_count = size;
		return std::u8string_view{ block->characters.data(), size };
	}

	auto SubEventSpecManager::FindSpecBlock(std::size_t index) -> SpecBlock*
	{
		return const_cast<SpecBlock*>(std::as_const(*this).FindSpecBlock(index));
	}

	auto SubEventSpecManager::FindSpecBlock(std::size_t index) const -> SpecBlock const*
	{
		// The blocks of the sequential queue begin at 0 and they are next to each other, so the block of the index
		// is the index divided by the capacity of a block.
		auto block_index = index / spec_block_capacity;
		if (block_index < sequential_spec_blocks.size())
		{
			return sequential_spec_blocks[block_index];
		}

		// The blocks of the random queue are not next to each other, every one of them carries the index which it
		// begins at, so the block of the index is found by searching the beginning of the blocks.
		for (auto& random_block : random_spec_blocks)
		{
			if (random_block.begin_index <= index && index < random_block.begin_index + spec_block_capacity)
			{
				return random_block.block;
			}
		}

		return nullptr;
	}

	auto SubEventSpecManager::CreateSpecBlock(std::size_t index) -> SpecBlock*
	{
		// The block is aligned to its capacity, so the index of the block inside it is the remainder of the index,
		// and the block of the index is found by the index divided by the capacity.
		auto begin_index = (index / spec_block_capacity) * spec_block_capacity;

		auto block = static_cast<SpecBlock*>(resource->allocate(sizeof(SpecBlock), alignof(SpecBlock)));
		// Every spec of the block is initialized by the default value of EventSpecView, so a spec which is not
		// stored yet carries no id, and the id of the spec tells whether it is assigned.
		std::construct_at(block);

		// The block is moved into the sequential queue when it is next to the last block of it, and it is kept in
		// the random queue otherwise, which finds the block of an index by the index which it begins at.
		if (begin_index == sequential_spec_blocks.size() * spec_block_capacity)
		{
			sequential_spec_blocks.push_back(block);

			// The new block may make the blocks of the random queue next to the sequential queue, so the random
			// queue is scanned again after a block is moved into the sequential queue, until none of its blocks
			// fits any more.
			bool moved = true;
			while (moved)
			{
				moved = false;
				for (auto ite = random_spec_blocks.begin(); ite != random_spec_blocks.end(); ++ite)
				{
					if (ite->begin_index == sequential_spec_blocks.size() * spec_block_capacity)
					{
						sequential_spec_blocks.push_back(ite->block);
						random_spec_blocks.erase(ite);
						moved = true;
						break;
					}
				}
			}
		}
		else {
			random_spec_blocks.push_back(RandomSpecBlock{ block, begin_index });
		}

		return block;
	}

	bool SubEventSpecManager::AllocateEventSpec(EventID id, EventSpecView*& spec)
	{
		spec = nullptr;

		if (!id)
		{
			return false;
		}

		// The block of the id is created when it does not exist yet, and the spec of the id is the slot of it
		// inside the block, the block of an index always begins at its aligned index.
		auto block = FindSpecBlock(id.id);
		if (block == nullptr)
		{
			block = CreateSpecBlock(id.id);
		}

		auto& ref = block->specs[id.id % spec_block_capacity];
		if (ref.id)
		{
			// The id is already stored, so the strings which are stored with it are kept.
			return false;
		}

		ref.id = id;
		spec = &ref;
		return true;
	}

	bool SubEventSpecManager::AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line)
	{
		// The strings of the spec are only stored when the id is added for the first time.
		EventSpecView* ref = nullptr;
		if (!AllocateEventSpec(id, ref))
		{
			return false;
		}

		// The name and the file of the spec are written into the blocks of the strings as they are, the wide
		// characters are encoded into UTF-8 while they are written.
		ref->name_view = StoreString(event_name);
		if (need_file)
		{
			ref->line = line;
			ref->file_view = StoreString(file);
		}
		return true;
	}

	bool SubEventSpecManager::AddEvent(EventID id, std::u8string_view event_name, std::u8string_view file, std::size_t line)
	{
		// The strings of the spec are only stored when the id is added for the first time.
		EventSpecView* ref = nullptr;
		if (!AllocateEventSpec(id, ref))
		{
			return false;
		}

		if (need_file)
		{
			ref->line = line;
		}

		// The name of the event of the CPU scope logger is already UTF-8, so it is written into the blocks of the
		// strings without any conversion.
		ref->name_view = StoreString(event_name);
		if (need_file)
		{
			ref->file_view = StoreString(file);
		}
		return true;
	}

	auto SubEventSpecManager::GetEventSpec(EventID id) const ->EventSpecView
	{
		if (id)
		{
			// The spec of the id is the slot of it inside the block, and a spec which is not assigned carries no
			// id, so the empty spec of an event which is not stored is not returned.
			auto block = FindSpecBlock(id.id);
			if (block != nullptr)
			{
				auto const& ref = block->specs[id.id % spec_block_capacity];
				if (ref.id)
				{
					return ref;
				}
			}
		}
		return {};
	}

	auto EventSpecManager::GetEventSpec(EventID id) const ->EventSpecView
	{
		if (id)
		{
			if (id.id > std::numeric_limits<std::uint32_t>::max() / 2)
			{
				auto real_uint32 = ~static_cast<std::uint32_t>(id.id);
				return metadata_events.GetEventSpec({ real_uint32 });
			}
			else {
				return events.GetEventSpec(id);
			}
		}
		return {};
	}

	bool EventSpecManager::AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line)
	{
		if (id.id > std::numeric_limits<std::uint32_t>::max() / 2)
		{
			auto real_uint32 = ~static_cast<std::uint32_t>(id.id);
			return metadata_events.AddEvent({ real_uint32 }, event_name, {}, 0);
		}

		return events.AddEvent(id, event_name, file, line);
	}

	bool EventSpecManager::AddEvent(EventID id, std::u8string_view event_name, std::u8string_view file, std::size_t line)
	{
		if (id.id > std::numeric_limits<std::uint32_t>::max() / 2)
		{
			auto real_uint32 = ~static_cast<std::uint32_t>(id.id);
			return metadata_events.AddEvent({ real_uint32 }, event_name, {}, 0);
		}

		return events.AddEvent(id, event_name, file, line);
	}

	void ContextSwitchEventList::AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time)
	{
		// The list of the thread is found with a linear search, and it is appended when an event of the
		// thread is added for the first time.
		auto find = std::find_if(thread_list.begin(), thread_list.end(), [thread_id](ThreadList const& list) {
			return list.thread_system_id == thread_id;
			});

		ThreadList* list = nullptr;
		if (find != thread_list.end())
		{
			list = &*find;
		}
		else {
			// The append may move the storage of the list, so the appended list is taken from the vector
			// itself instead of the iterator which was searched with.
			list = &thread_list.emplace_back(thread_id, &resource);
		}

		list->AddEvent(ThreadList::Event{ active_core, active_time });
	}

	ContextSwitchEventList::ThreadList::ThreadList(ThreadSystemID in_thread_id, std::pmr::memory_resource* in_resource)
		: thread_system_id(in_thread_id)
		, blocks(in_resource)
		, fast_check_point(in_resource)
		, resource(in_resource)
	{
	}

	ContextSwitchEventList::ThreadList::~ThreadList()
	{
		// The blocks are owned by the list, the storage of a block is freed to the resource which it was
		// allocated from.
		for (auto block : blocks)
		{
			block->~EventBlock();
			resource->deallocate(block, sizeof(EventBlock), alignof(EventBlock));
		}
	}

	std::size_t ContextSwitchEventList::ThreadList::GetEventCount() const
	{
		// The count is cached by the list, it is never computed from the blocks.
		return event_count;
	}

	auto ContextSwitchEventList::ThreadList::GetEvent(std::size_t index) const -> Event const&
	{
		assert(index < GetEventCount());
		return blocks[index / event_block_capacity]->events[index % event_block_capacity];
	}

	void ContextSwitchEventList::ThreadList::AddEvent(Event event)
	{
		// The position of the event inside its block, an aligned position means that the last block is full
		// and a new one has to be allocated. The first event of the thread takes the position 0 as well,
		// which is the first block.
		std::size_t index_in_block = event_count % event_block_capacity;
		if (index_in_block == 0)
		{
			// The block is only allocated when the last one is full, so the events which are already stored
			// are never moved.
			auto block = static_cast<EventBlock*>(resource->allocate(sizeof(EventBlock), alignof(EventBlock)));
			std::construct_at(block);
			blocks.push_back(block);

			// The fast check point of the block is the time of its first event, and it is recorded when the
			// block is created, so the coarse search skips the blocks which are entirely before the target
			// point. The check point is the boundary which the block starts at, so the first block has none,
			// it is the block which the search starts with.
			if (event_count != 0)
			{
				fast_check_point.emplace_back(event.active_time_range.Begin());
			}
		}

		auto& block = *blocks.back();
		block.events[index_in_block] = event;
		event_count += 1;
	}

	std::size_t ContextSwitchEventList::ThreadList::FastLocateFirstEventIndex(DurationSec target_point) const
	{
		// Every check point is the boundary which a block starts at, so the count of the check points which
		// the target point has reached is the index of the block which the search has to start with.
		std::size_t start_index = 0;
		for (; start_index < fast_check_point.size(); ++start_index)
		{
			if (fast_check_point[start_index] > target_point)
				break;
		}
		return start_index * event_block_capacity;
	}
	std::size_t ContextSwitchEventList::ThreadList::LocateFirstEventIndex(DurationSec target_point, std::size_t index_offset) const
	{
		auto event_count = GetEventCount();
		for (; index_offset < event_count; ++index_offset)
		{
			if (GetEvent(index_offset).active_time_range.End() > target_point)
			{
				break;
			}
		}
		return index_offset;
	}

	auto ContextSwitchEventList::ThreadList::GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const ->Statis
	{
		auto event_count = GetEventCount();
		auto fast_start = FastLocateFirstEventIndex(time_range.Begin());
		auto start = LocateFirstEventIndex(time_range.Begin(), fast_start);
		
		if (start >= event_count)
		{
			return Statis{
				time_range.Size(),
				0
			};
		}
		
		auto end = LocateFirstEventIndex(time_range.End(), start);

		if (end >= event_count)
		{
			end = event_count - 1;
		}

		std::size_t active_count = 0;
		DurationSec active_time = DurationSec::zero();

		DurationSec overlapping_time = DurationSec::zero();

		auto start_time = GetEvent(start).active_time_range.Begin();
		auto end_time_range = GetEvent(end).active_time_range;

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
			total_duration += GetEvent(i).active_time_range.Size();
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
		auto find = std::find_if(thread_list.begin(), thread_list.end(), [thread_id](ThreadList const& list) {
			return list.thread_system_id == thread_id;
			});
		if (find != thread_list.end())
		{
			return find->GetContextSwitchStatis(time_range);
		}
		return {time_range.Size(), 0};
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

	ThreadCPUEventView::EventView ThreadCPUEventView::FindChildEvent(std::span<EventID const> event_id_span, EventView parent_event) const
	{
		if (parent_event)
		{
			return FindNextEvent(event_id_span, { parent_event.index_range.Begin() + 1, parent_event .index_range.End()});
		}
		else {
			return FindNextEvent(event_id_span);
		}
	}

	ThreadCPUEventView::EventView ThreadCPUEventView::FindParentEvent(std::span<EventID const> event_id_span, EventView child_event) const
	{
		if (child_event && child_event.depth != 0)
		{
			assert(child_event.index_range.Begin() > 0);
			auto iterator = child_event.index_range.Begin();
			do {
				--iterator;
				auto cur = view[iterator];
				if (
					cur.event_id && cur.depth < child_event.depth
					&& (event_id_span.empty() || std::find(event_id_span.begin(), event_id_span.end(), cur.event_id) != event_id_span.end())
					)
				{
					auto find = FindFirstDepth(cur.depth, iterator + 1);
					assert(find);
					return EventView{
						cur.event_id,
						{iterator, find.index_offset},
						{cur.time, find.time_point},
						cur.depth
					};
				}
			} while (iterator > 0);
		}
		return {};
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
		// The statis of a range is asked from the list, the walk of the ranges is the same as the walk with any
		// provider of the statis, so it is shared with the listener of the streamer of the context switch events.
		return GetExcludeTime(view, [this, &list](Potato::Misc::IndexSpan<DurationSec> range) {
			return list.GetContextSwitchStatis(system_thread_id, range);
		});
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
		OnThreadDiscovered(thread_id, thread_name_str);
	}
	*/

	/*
	uint32 ParserInterface::OnCPUEventDiscovered(wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line)
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
		OnCPUEventDiscovered(EventID{ event_id }, cur_event_name, cur_file_name, line);
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

	void ParserInterface::OnThreadDiscovered(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len)
	{
		std::string_view thread_name_view{ thread_name, thread_name_len };
		thread_timelines.emplace_back(
			ThreadID{ thread_id },
			ThreadSystemID{ thread_system_id },
			std::string{ thread_name, thread_name_len },
			std::unique_ptr<ParserThreadTimeLine>{}
		);
		OnThreadDiscovered(ThreadID{ thread_id }, ThreadSystemID{ thread_system_id }, thread_name_view);
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

	bool GameFrameRecoder::RecordEvent(EventID id, std::wstring_view event_name)
	{
		if (!id)
		{
			return false;
		}

		// The names are written into the recorder, so only the two of them are accepted.
		if (event_name == L"FEngineLoop::Tick")
		{
			if (std::find(tick_event_ids.begin(), tick_event_ids.end(), id) == tick_event_ids.end())
			{
				tick_event_ids.push_back(id);
			}
			return true;
		}

		if (event_name == L"Frame")
		{
			if (std::find(frame_event_ids.begin(), frame_event_ids.end(), id) == frame_event_ids.end())
			{
				frame_event_ids.push_back(id);
			}
			return true;
		}

		return false;
	}

	bool GameFrameRecoder::RecordEvent(EventID id, std::u8string_view event_name)
	{
		if (!id)
		{
			return false;
		}

		// The name of the event of the CPU scope logger is UTF-8, so it is compared with the watched names without
		// any conversion.
		if (event_name == tick_event_name)
		{
			if (std::find(tick_event_ids.begin(), tick_event_ids.end(), id) == tick_event_ids.end())
			{
				tick_event_ids.push_back(id);
			}
			return true;
		}

		if (event_name == frame_event_name)
		{
			if (std::find(frame_event_ids.begin(), frame_event_ids.end(), id) == frame_event_ids.end())
			{
				frame_event_ids.push_back(id);
			}
			return true;
		}

		return false;
	}

	bool GameFrameRecoder::RecordThread(ThreadID id, ThreadSystemID system_id, std::string_view thread_name)
	{
		// The thread which the frames are recorded on is written into the recorder, so no other thread is accepted.
		if (!id || thread_name != game_thread_name)
		{
			return false;
		}

		game_thread_id = id;
		game_thread_system_id = system_id;
		return true;
	}

	bool GameFrameRecoder::IsTickEvent(EventID id) const
	{
		return id && std::find(tick_event_ids.begin(), tick_event_ids.end(), id) != tick_event_ids.end();
	}

	bool GameFrameRecoder::IsFrameEvent(EventID id) const
	{
		return id && std::find(frame_event_ids.begin(), frame_event_ids.end(), id) != frame_event_ids.end();
	}

	bool GameFrameRecoder::IsFrameBeginningEvent(EventID id) const
	{
		return IsTickEvent(id) || IsFrameEvent(id);
	}

	bool GameFrameRecoder::IsFrameBeginningEvent(ThreadCPUEventView event_scope) const
	{
		// The stack tree begins with the event which begins the frame, and the other stack trees of the thread are
		// the trees of the events which are not a frame.
		if (event_scope.view.empty())
		{
			return false;
		}

		return IsFrameBeginningEvent(event_scope.view.front().event_id);
	}

	bool GameFrameRecoder::IsGameThread(ThreadID id) const
	{
		return id && id == game_thread_id;
	}

	GameFrameRecoder::GameFrameRecoder(Config config)
		: frame_blocks(config.resource)
		, fast_check_point(config.resource)
		, resource(config.resource)
	{
	}

	GameFrameRecoder::~GameFrameRecoder()
	{
		// The blocks are owned by the recorder, the storage of a block is freed to the resource which it was
		// allocated from.
		for (auto block : frame_blocks)
		{
			block->~FrameBlock();
			resource->deallocate(block, sizeof(FrameBlock), alignof(FrameBlock));
		}
	}

	auto GameFrameRecoder::GetFrameTimeRange(std::size_t index) const -> Potato::Misc::IndexSpan<DurationSec> const&
	{
		assert(index < frame_count);
		return frame_blocks[index / frame_block_capacity]->frames[index % frame_block_capacity];
	}

	void GameFrameRecoder::AddFrameTimeRange(Potato::Misc::IndexSpan<DurationSec> time_range)
	{
		// The position of the frame inside its block, an aligned position means that the last block is full
		// and a new one has to be allocated. The first frame takes the position 0 as well, which is the first
		// block.
		std::size_t index_in_block = frame_count % frame_block_capacity;
		if (index_in_block == 0)
		{
			// The block is only allocated when the last one is full, so the frames which are already stored
			// are never moved.
			auto block = static_cast<FrameBlock*>(resource->allocate(sizeof(FrameBlock), alignof(FrameBlock)));
			std::construct_at(block);
			frame_blocks.push_back(block);

			// The fast check point of the block is the time of its first frame, and it is recorded when the
			// block is created, so the coarse search skips the blocks which are entirely before the target
			// point. The first block has none, it is the block which the search starts with.
			if (frame_count != 0)
			{
				fast_check_point.emplace_back(time_range.Begin());
			}
		}

		frame_blocks.back()->frames[index_in_block] = time_range;
		frame_count += 1;
	}

	std::size_t GameFrameRecoder::FastLocateFirstFrameIndex(DurationSec time_point) const
	{
		// Every check point is the boundary which a block starts at, so the count of the check points which
		// the target point has reached is the index of the block which the search has to start with.
		std::size_t block_index = 0;
		for (; block_index < fast_check_point.size(); ++block_index)
		{
			if (fast_check_point[block_index] > time_point)
				break;
		}
		return block_index * frame_block_capacity;
	}

	std::size_t GameFrameRecoder::LocateFirstFrameEndIndex(DurationSec time_point, std::size_t index_offset) const
	{
		for (; index_offset < frame_count; ++index_offset)
		{
			if (GetFrameTimeRange(index_offset).End() > time_point)
			{
				break;
			}
		}
		return index_offset;
	}

	std::size_t GameFrameRecoder::LocateFirstFrameBeginIndex(DurationSec time_point, std::size_t index_offset) const
	{
		for (; index_offset < frame_count; ++index_offset)
		{
			if (GetFrameTimeRange(index_offset).Begin() >= time_point)
			{
				break;
			}
		}
		return index_offset;
	}

	auto GameFrameRecoder::GetTotalTimeRange() const -> std::optional<Potato::Misc::IndexSpan<DurationSec>>
	{
		if (frame_count == 0)
		{
			return std::nullopt;
		}

		// The frames are recorded in the analysis order, so the total range is from the begin of the first
		// frame to the end of the last one.
		return Potato::Misc::IndexSpan<DurationSec>{
			GetFrameTimeRange(0).Begin(),
			GetFrameTimeRange(frame_count - 1).End()
		};
	}

	std::size_t GameFrameRecoder::LocateFirstFrameIndex(DurationSec time_point) const
	{
		return LocateFirstFrameEndIndex(time_point, FastLocateFirstFrameIndex(time_point));
	}

	auto GameFrameRecoder::GetFrameIndexRange(Potato::Misc::IndexSpan<DurationSec> time_range) const -> Potato::Misc::IndexSpan<>
	{
		if (frame_count == 0)
		{
			return {0, 0};
		}

		// The range is on the right of the last frame, or on the left of the first frame.
		if (time_range.Begin() >= GetFrameTimeRange(frame_count - 1).End())
		{
			return {0, 0};
		}
		if (time_range.End() <= GetFrameTimeRange(0).Begin())
		{
			return {0, 0};
		}

		// The first frame which the range covers is the first frame whose end is after the begin of the range.
		std::size_t start = LocateFirstFrameIndex(time_range.Begin());
		if (start >= frame_count)
		{
			return {0, 0};
		}

		// The first frame which begins at or after the end of the range is the first frame which the range
		// does not cover, so the frames between the start and it are the frames which the range covers.
		std::size_t end_offset = std::max(start, FastLocateFirstFrameIndex(time_range.End()));
		std::size_t end = LocateFirstFrameBeginIndex(time_range.End(), end_offset);
		if (end <= start)
		{
			return {0, 0};
		}

		return {start, end};
	}

	bool GameFrameRecoder::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		// The frame is a stack tree of the game thread only, and it begins with one of the watched events.
		if (!IsGameThread(event_scope.thread_id) || !IsFrameBeginningEvent(event_scope))
		{
			return false;
		}

		// The time range of the frame is the whole time range of its stack tree.
		auto time_range = event_scope.GetTimeRange();
		if (!time_range)
		{
			return false;
		}

		AddFrameTimeRange(*time_range);
		return true;
	}
}