module;

#include "UEBabyPramInsightParserInterface.h"

export module UEBabyPramInsightParser;
import std;
import Potato;

export namespace UEBabyPram::InsightParser
{
	struct EventID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(EventID const&) const = default;
		bool operator==(EventID const&) const = default;
	};

	struct ThreadID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(ThreadID const&) const = default;
		bool operator==(ThreadID const&) const = default;
	};

	struct ThreadSystemID
	{
		std::size_t id = std::numeric_limits<std::size_t>::max();
		operator bool() const { return id != std::numeric_limits<std::size_t>::max(); }
		std::strong_ordering operator<=>(ThreadSystemID const&) const = default;
		bool operator==(ThreadSystemID const&) const = default;
	};

	using DurationSec = std::chrono::duration<double, std::ratio<1, 1>>;

	// A time point (or duration) which is displayed as "<minutes>m<seconds>s",
	// e.g. 90.5 seconds is displayed as "1m30.500000s".
	struct DisplayTimePoint
	{
		std::chrono::minutes minutes = std::chrono::minutes::zero();
		DurationSec seconds = DurationSec::zero();
		DisplayTimePoint(DurationSec duration);
	};

}

export namespace std
{
	template<>
	struct hash<UEBabyPram::InsightParser::EventID>
	{
		std::size_t operator()(UEBabyPram::InsightParser::EventID key) const { return key; }
	};

	template<>
	struct hash<UEBabyPram::InsightParser::ThreadID>
	{
		std::size_t operator()(UEBabyPram::InsightParser::ThreadID key) const { return key.id; }
	};

	template<>
	struct hash<UEBabyPram::InsightParser::ThreadSystemID>
	{
		std::size_t operator()(UEBabyPram::InsightParser::ThreadSystemID key) const { return key; }
	};

	// Format a DisplayTimePoint as "[minutes]m[seconds]s", where the minutes are printed
	// as an integer and the seconds are printed with 6 digits after the decimal point,
	// i.e. the output looks like "1m30.500000s". No format spec is accepted.
	template<typename CharT>
	struct formatter<UEBabyPram::InsightParser::DisplayTimePoint, CharT>
	{
		constexpr auto parse(std::basic_format_parse_context<CharT>& parse_context)
		{
			if (*parse_context.begin() == static_cast<CharT>('}'))
				return parse_context.begin();
			throw std::format_error("std::formatter<UEBabyPram::InsightParser::DisplayTimePoint> does not accept any format spec");
		}

		template<typename FormatContext>
		auto format(UEBabyPram::InsightParser::DisplayTimePoint time_point, FormatContext& format_context) const
		{
			if constexpr (std::is_same_v<CharT, char>)
			{
				return std::format_to(
					format_context.out(), "{:}m{:.6f}s",
					time_point.minutes.count(), time_point.seconds.count()
				);
			}
			else
			{
				return std::format_to(
					format_context.out(), L"{:}m{:.6f}s",
					time_point.minutes.count(), time_point.seconds.count()
				);
			}
		}
	};
}

export namespace UEBabyPram::InsightParser
{
	using UEBabyPram::InsightParser::DataResourceInterface;

	struct ContextSwitchEventList
	{

		struct Config
		{
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		void AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time);
		
		struct Statis
		{
			// The time of the range with the context switch time removed, i.e. the time which the
			// thread really spent on the CPU in the range.
			DurationSec removed_context_switch_time = DurationSec::zero();
			std::size_t switch_count = 0;
		};

		Statis GetContextSwitchStatis(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationSec> time_range) const;

		ContextSwitchEventList(Config config = {}) : resource(config.resource), thread_list(&resource) {}

	protected:

		std::pmr::unsynchronized_pool_resource resource;

		struct ThreadList
		{
			// How many events one block of the events holds.
			static constexpr std::size_t event_block_capacity = 1024 * 16;

			struct Event
			{
				std::size_t active_core = std::numeric_limits<std::size_t>::max();
				Potato::Misc::IndexSpan<DurationSec> active_time_range;
			};

			// One block of the events, the events of the block are filled from its beginning, so the block
			// does not have to keep its own count, the count of the thread tells how many events of the last
			// block are used.
			struct EventBlock
			{
				std::array<Event, event_block_capacity> events;
			};

			ThreadList(ThreadSystemID thread_id, std::pmr::memory_resource* resource);
			ThreadList(ThreadList const& list) = delete;
			ThreadList(ThreadList&& list) = default;
			~ThreadList();

			// The count of the events of the thread, it is cached by the list itself so it never has to be
			// computed from the blocks, and the event at the index of it.
			std::size_t GetEventCount() const;
			Event const& GetEvent(std::size_t index) const;
			// Append one event, the block of the events is only allocated when the last one is full, and the
			// fast check point of the block is recorded with the block itself.
			void AddEvent(Event event);

			ThreadSystemID thread_system_id;
			// How many events of the thread are stored, it is the position where the next event is added to
			// as well, the events of a block are filled one by one from its beginning.
			std::size_t event_count = 0;
			// The blocks of the events, the last one is the block which the next event is added to.
			std::pmr::vector<EventBlock*> blocks;
			// The boundary which every block of the events starts at, which is the time of the first event of
			// the block, so the coarse search of the events skips the blocks which are entirely before the
			// target point. The first block has no boundary, so the count of the entries is the count of the
			// blocks minus one, and the entry of a block is added when the block is created.
			std::pmr::vector<DurationSec> fast_check_point;
			std::size_t FastLocateFirstEventIndex(DurationSec target_point) const;
			std::size_t LocateFirstEventIndex(DurationSec target_point, std::size_t index_offset) const;
			Statis GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const;
			// The resource which the blocks of the events are allocated from and freed to.
			std::pmr::memory_resource* resource;
		};

		// The events of the context switch of every thread are kept in the blocks of
		// ThreadList::event_block_capacity events: a block is only allocated when the last one is full, so
		// an event which is added never moves the events which are already stored, and the memory of the
		// events which are not added yet is not taken at all. The fast check points of the thread are kept
		// with the same granularity, one boundary per block, so the block is both the storage unit and the
		// search step of the events.
		// Every thread keeps its own list, the list is found with a linear search by the thread id, and a
		// thread which is not in the list yet is appended when one of its events is added for the first
		// time.
		std::pmr::vector<ThreadList> thread_list;
	};

	struct SubEventSpecManager
	{
		// How many characters one block of the strings holds.
		static constexpr std::size_t string_block_capacity = 1024 * 16;
		// How many specs one block of the specs holds.
		static constexpr std::size_t spec_block_capacity = 4096;

		struct Config
		{
			bool need_file = true;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The spec of one event, which is also the storage of the spec: the name and the file are the views
		// of the blocks of the strings, so they stay valid while the manager is alive.
		struct EventSpecView
		{
			EventID id;
			std::u8string_view name_view = u8"UnknowEvent";
			std::u8string_view file_view;
			std::size_t line = 0;
			operator bool() const { return id; }
		};

		SubEventSpecManager(Config config = {});
		SubEventSpecManager(SubEventSpecManager const&) = delete;
		SubEventSpecManager(SubEventSpecManager&&) = default;
		~SubEventSpecManager();

		// Store the name and the file of one event, false is returned when the event is already stored.
		bool AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line);
		bool AddEvent(EventID id, std::u8string_view event_name, std::u8string_view file, std::size_t line);
		EventSpecView GetEventSpec(EventID id) const;

		// Visit every stored event spec through the visitor, the order is unspecified.
		// The string views of the visited EventSpecView point into the blocks of the strings, which are never
		// moved, so they stay valid while the manager is alive.
		template<typename VisitorT>
		void ForEachEvent(VisitorT&& visitor) const
		{
			for (auto block : sequential_spec_blocks)
			{
				for (auto& spec : block->specs)
				{
					if (spec.id)
					{
						visitor(spec);
					}
				}
			}
			for (auto& random_block : random_spec_blocks)
			{
				for (auto& spec : random_block.block->specs)
				{
					if (spec.id)
					{
						visitor(spec);
					}
				}
			}
		}

	protected:

		// The characters of the strings of the events, they are kept in the blocks of string_block_capacity
		// characters. A string is written into the block which has room for it, and a string never crosses
		// the boundary of a block, so the characters of a string are always a slice of one block.
		struct StringBlock
		{
			std::array<char8_t, string_block_capacity> characters;
		};

		// One block of the specs, it always begins at its aligned index, which means that the block of the spec
		// index is the index divided by spec_block_capacity, and the spec inside the block is the remainder.
		struct SpecBlock
		{
			std::array<EventSpecView, spec_block_capacity> specs;
		};

		// One block of the random queue, which carries the index of the first spec of the block, so the block of
		// an index is found without knowing which block the index belongs to.
		struct RandomSpecBlock
		{
			SpecBlock* block = nullptr;
			std::size_t begin_index = 0;
		};

		// Find the block which the spec index is in, or nullptr when the block is not created yet.
		SpecBlock* FindSpecBlock(std::size_t index);
		SpecBlock const* FindSpecBlock(std::size_t index) const;
		// Create the block which the spec index is in, and put it into the queue which it belongs to.
		SpecBlock* CreateSpecBlock(std::size_t index);

		// The slot of the spec which the id is stored in, its block is created when the block of the id does not
		// exist yet, and nullptr is returned when the spec of the id is already assigned.
		EventSpecView* AllocateEventSpec(EventID id);

		// Write the name or the file of the event into the blocks of the strings and return the view of it.
		// The string which is longer than a quarter of the block is replaced by MakeReplacedString(), so the
		// characters of every string are written into one block: a quarter of the block is the worst case of
		// the encoding, which is four bytes of UTF-8 for one wide character.
		std::u8string_view StoreString(std::u8string_view string, EventID id, bool is_file);
		std::u8string_view StoreString(std::wstring_view string, EventID id, bool is_file);
		// Write the characters of the source into the blocks of the strings, the characters are encoded into
		// UTF-8 while they are written, and the view of the written characters is returned.
		template<typename FromCharT>
		std::u8string_view WriteString(std::span<FromCharT const> string);
		// The fixed string which replaces the string which is too long for a block of the strings, it carries
		// the id of the event and whether it is the file of the event, so the replaced strings stay told apart.
		// The replacement is written into encoded_string, which is why the view of it is returned, the
		// characters of it are ASCII, so the view of it is UTF-8 without any conversion.
		std::u8string_view MakeReplacedString(EventID id, bool is_file);

		const bool need_file;
		// The blocks of the specs whose ids follow each other from 0: the block of the spec index is the index
		// divided by spec_block_capacity, so the block of an id is found without any search.
		std::pmr::vector<SpecBlock*> sequential_spec_blocks;
		// The blocks of the specs whose ids are not next to the last block of the sequential queue, every block
		// carries the index which it begins at, so the block of an id is found by searching the blocks.
		std::pmr::vector<RandomSpecBlock> random_spec_blocks;
		// The blocks of the characters, they own the characters of the strings of the events.
		std::pmr::vector<StringBlock*> string_blocks;
		// The block which is being written, it is where the next string is written into, and it is empty when no
		// string is stored yet.
		StringBlock* current_block = nullptr;
		// How many characters are written into current_block, which is where the next character is written to.
		// The count is kept here instead of the block, because it only means something together with the block
		// which is being written.
		std::size_t current_block_count = 0;
		// The resource which the blocks of the characters are allocated from and freed to.
		std::pmr::memory_resource* resource;
		// The storage of the fixed string of MakeReplacedString(), it is reused, so the replacement of a string
		// which is too long does not allocate. The characters of the replacement are ASCII, so the string is
		// viewed as UTF-8 without any conversion.
		std::pmr::string encoded_string;
	};

	struct EventSpecManager
	{

		using EventSpecView = typename SubEventSpecManager::EventSpecView;

		struct Config
		{
			bool need_file = true;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		EventSpecManager(Config config = {})
			: events(
				SubEventSpecManager::Config{ config.need_file, config.resource }
			),
			metadata_events(
				SubEventSpecManager::Config{ false, config.resource }
			)
		{

		}

		EventSpecManager(EventSpecManager&&) = default;
		EventSpecManager(EventSpecManager const&) = delete;

		EventSpecView GetEventSpec(EventID id) const;

		// The name and the file of the interface are stored as they are, the characters are encoded into UTF-8
		// while they are written into the blocks of the strings of the manager of the events.
		bool AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line);
		bool AddEvent(EventID id, std::u8string_view event_name, std::u8string_view file, std::size_t line);

		// Visit every event spec which is managed by this manager, the order is unspecified.
		// The string views of the visited EventSpecView point into the blocks of the strings of the managers
		// below, so they stay valid while this manager is alive.
		template<typename VisitorT>
		void ForEachEvent(VisitorT&& visitor) const
		{
			events.ForEachEvent(visitor);
			metadata_events.ForEachEvent(visitor);
		}

	protected:

		SubEventSpecManager events;
		SubEventSpecManager metadata_events;
	};

	struct DcomentWrapper : public UEBabyPram::InsightParser::DataResourceInterface
	{
		DcomentWrapper(Potato::Document::DocumentReader& reader) : reader(reader) {}
		virtual std::int32_t Read(void* out_data, std::uint32_t byte_size) override
		{
			return static_cast<std::int32_t>(reader.StreamRead(static_cast<std::byte*>(out_data), byte_size));
		}
	protected:
		Potato::Document::DocumentReader& reader;
	};

	struct ThreadCPUEvent
	{
		EventID event_id;
		DurationSec time;
		std::size_t depth;
	};

	struct ThreadCPUEventView
	{
		ThreadID thread_id;
		ThreadSystemID system_thread_id;
		std::span<ThreadCPUEvent const> view;
		std::optional<std::size_t> frame_count;

		struct FindResult
		{
			EventID event_id;
			std::size_t index_offset = std::numeric_limits<std::size_t>::max();
			std::size_t depth;
			DurationSec time_point;
			operator bool() const { return index_offset != std::numeric_limits<std::size_t>::max(); }
		};

		FindResult FindFirstEventID(std::span<EventID const> event_ids, std::size_t find_offset = 0) const
		{
			return FindFirstEventID(event_ids, Potato::Misc::IndexSpan<>{find_offset, std::numeric_limits<std::size_t>::max()});
		}
		FindResult FindFirstEventID(std::span<EventID const> event_ids, Potato::Misc::IndexSpan<> index_range) const;
		FindResult FindFirstDepth(std::size_t depth, std::size_t find_offset = 0) const
		{
			return FindFirstDepth(depth, Potato::Misc::IndexSpan<>{find_offset, std::numeric_limits<std::size_t>::max()});
		}
		FindResult FindFirstDepth(std::size_t depth, Potato::Misc::IndexSpan<> index_range) const;

		struct EventView
		{
			EventID event_id;
			Potato::Misc::IndexSpan<> index_range;
			Potato::Misc::IndexSpan<DurationSec> time_range;
			std::size_t depth = std::numeric_limits<std::size_t>::max();
			operator bool() const { return event_id; }
		};

		EventView FindNextEvent(std::span<EventID const> event_id_span, std::size_t offset = 0) const
		{
			return FindNextEvent(event_id_span, Potato::Misc::IndexSpan<>{offset, std::numeric_limits<std::size_t>::max()});
		}
		
		EventView FindNextEvent(std::span<EventID const> event_id_span, Potato::Misc::IndexSpan<> index_range) const;

		EventView FindChildEvent(std::span<EventID const> event_id_span, EventView parent_event = {}) const;
		EventView FindParentEvent(std::span<EventID const> event_id_span, EventView child_event) const;

		struct ExcludeTimeStatis
		{
			DurationSec active_time = DurationSec::zero();
			DurationSec active_time_without_context_switch = DurationSec::zero();
			std::size_t context_switch_count = 0;
		};

		std::optional<ExcludeTimeStatis> GetExcludeTime(EventView view) const;
		std::optional<ExcludeTimeStatis> GetExcludeTime(EventView view, ContextSwitchEventList const& list) const;

		EventID GetTopEvent() const;
		
		std::optional<Potato::Misc::IndexSpan<DurationSec>> GetTimeRange() const {
			if (view.size() > 0)
			{
				return Potato::Misc::IndexSpan<DurationSec>{ view.begin()->time, view.rbegin()->time };
			}
			return std::nullopt;
		}
	};

	struct ParserInterface;

	struct ParserThreadTimeLine : public ThreadTimeLineInterface
	{
		ParserThreadTimeLine(ThreadID thread_id, ThreadSystemID thread_system_id, ParserInterface& reference) 
			: thread_id(thread_id), thread_system_id(thread_system_id), reference(reference) {}
		virtual void AppendBeginEvent(double start_time, std::uint32_t event_id) override;
		virtual void AppendEndEvent(double end_time) override;
		ThreadID thread_id;
		ThreadSystemID thread_system_id;
		std::optional<std::size_t> frame_id;
		std::size_t depth = 0;
		std::vector<ThreadCPUEvent> stacks;
		DurationSec last_time = DurationSec::zero();
		ParserInterface& reference;
		~ParserThreadTimeLine();
	};

	struct ParserInterface : private BaseParser
	{
		virtual bool IsParserRequired(ParserRequireFlag flag) const override { return true; }
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) {}
		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) {}
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) {}
		// Called when a CPU event spec ("CpuProfiler/EventSpec") is discovered.
		// Requires ParserRequireFlag::EVENT_SPEC, it is not called without that flag.
		// Note: the same event_name may correspond to multiple EventIDs (the same name can be
		// declared more than once), but the same EventID is reported only once.
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) {}
		// Called when a CPU scoped event (a "Cpu" logger scope) is discovered.
		// Requires ParserRequireFlag::META_DATA.
		// Note: contrary to OnCPUEventDiscovered, the same EventID shows up multiple times
		// (once per scope instance), so implementations must tolerate repeated ids.
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) {}
		virtual	void AllAnalyzeDone() override;
		virtual bool IsThreadRequired(ThreadID thread_id) const { return true; }
		static std::wstring_view CoverStringView(wchar_t const* ScopeName, std::size_t ScopeNameLen);

		struct CPUEventInfo
		{
			EventID id;
			std::wstring_view event_name;
			std::wstring_view file_name;
			std::size_t file_line;
		};

		struct ThreadInfo
		{
			ThreadID thread_id;
			ThreadSystemID thread_system_id;
			std::string_view thread_name;
		};

		std::optional<CPUEventInfo> GetCPUEventInfo(EventID event_id) const;
		std::optional<ThreadInfo> GetThreadInfo(ThreadID thread_id) const;
		std::optional<ThreadInfo> GetThreadInfo(ThreadSystemID thread_system_id) const;

	private:
		
		virtual void OnCPUEventDiscovered(uint32_t space_id, wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) override
		{
			return OnCPUEventDiscovered(
				EventID{ space_id }, std::wstring_view{ event_name, event_name_len }, std::wstring_view{ file, file_name_len }, line
			);
		}

		virtual void OnCPUScopeEventDiscovered(uint32_t space_id, char const* event_name, std::size_t event_name_len) override
		{
			return OnCPUScopeEventDiscovered(
				EventID{ space_id }, std::u8string_view{ reinterpret_cast<char8_t const*>(event_name), event_name_len }
			);
		}

		virtual ParserThreadTimeLine* GetThreadTimeLine(uint32 thread_id) override;

		virtual uint32 AddMetaDataLayout(wchar_t const* format, wchar_t const* const* field_names, std::size_t field_names_len) override { return 0; }

		//virtual void SetMetadataSpec(uint32 event_id, uint32 metadata_space_id) override {}
		virtual bool IsThreadRequired(uint32 thread_id) const { return IsThreadRequired(ThreadID(thread_id)); }
		//virtual uint32 AddMetaData(uint32 event_id, MetaDataFormat format, uint8 const* data, std::size_t meta_data_len, uint32 thread_id) override;
		//virtual void SetMetadata(uint32 MetaDataId, MetaDataFormat format, uint8 const* meta_data, std::size_t meta_data_len, uint32 TimerId, uint32 ThreadId) override;
		virtual void ContextSwitchEvent(uint32 thread_id, uint32 active_core, double active_start_time, double active_end_time) override {
			return ContextSwitchEvent(ThreadSystemID{ thread_id }, active_core, Potato::Misc::IndexSpan<DurationSec>{DurationSec{ active_start_time }, DurationSec{ active_end_time }});
		}

		virtual void OnThreadDiscovered(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len) override;
		
		struct TimeLineTuple
		{
			ThreadID thread_id;
			ThreadSystemID thread_system_id;
			std::string thread_name;
			std::unique_ptr<ParserThreadTimeLine> time_line;
		};

		std::vector<TimeLineTuple> thread_timelines;

		struct CPUEvent
		{
			EventID id;
			std::wstring event_name;
			std::wstring file_name;
			std::size_t file_line;
		};

		std::unordered_map<EventID, CPUEvent> events;
		
		std::vector<CPUEvent> time_infos;
		std::vector<EventID> frame_event_id;

		//virtual void AddThread(uint32 thread_id, char const* thread_name);
		friend void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser);
	};

	void ExecuteParser(Potato::Document::DocumentReader& Resource, ParserInterface& Parser)
	{
		DcomentWrapper ResourceWrapper(Resource);
		ExecuteParser(ResourceWrapper, Parser);
	}
}


//export import UEBabyPramInsightInterface;