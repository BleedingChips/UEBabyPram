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

}

export namespace std
{
	template<>
	struct hash<UEBabyPram::InsightParser::EventID>
	{
		std::size_t operator()(UEBabyPram::InsightParser::EventID key) const { return key; }
	};

	template<>
	struct hash<UEBabyPram::InsightParser::ThreadSystemID>
	{
		std::size_t operator()(UEBabyPram::InsightParser::ThreadSystemID key) const { return key; }
	};
}

export namespace UEBabyPram::InsightParser
{
	using UEBabyPram::InsightParser::DataResourceInterface;
	

	struct DisplayDuration
	{
		std::chrono::minutes minutes = std::chrono::minutes::zero();
		DurationSec seconds = DurationSec::zero();
		DisplayDuration(DurationSec duration);
	};

	struct ContextSwitchEventList
	{

		struct Config
		{
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		void AddContextSwitchEvent(ThreadSystemID thread_id, std::size_t active_core, Potato::Misc::IndexSpan<DurationSec> active_time);
		
		struct Statis
		{
			DurationSec active_time = DurationSec::zero();
			std::size_t switch_count = 0;
		};

		Statis GetContextSwitchStatis(ThreadSystemID thread_id, Potato::Misc::IndexSpan<DurationSec> time_range) const;

		ContextSwitchEventList(Config config = {}) : resource(config.resource), thread_list(&resource) {}

	protected:

		std::pmr::unsynchronized_pool_resource resource;

		struct ThreadList
		{
			ThreadList(ThreadSystemID thread_id, std::size_t fast_check_point_count, std::pmr::memory_resource* resource) :
				thread_system_id(thread_id), fast_check_point_count(fast_check_point_count), events(resource), fast_check_point(resource)
			{ }
			ThreadList(ThreadList const& list) = default;
			ThreadList(ThreadList&&) = default;

			ThreadSystemID thread_system_id;
			struct Event
			{
				std::size_t active_core = std::numeric_limits<std::size_t>::max();
				Potato::Misc::IndexSpan<DurationSec> active_time_range;
			};
			std::pmr::vector<Event> events;
			std::pmr::vector<DurationSec> fast_check_point;
			std::size_t FastLocateFirstEventIndex(DurationSec target_point) const;
			std::size_t LocateFirstEventIndex(DurationSec target_point, std::size_t index_offset) const;
			Statis GetContextSwitchStatis(Potato::Misc::IndexSpan<DurationSec> time_range) const;
			const std::size_t fast_check_point_count;
		};
		std::pmr::unordered_map<ThreadSystemID, ThreadList> thread_list;
		const std::size_t fast_check_point_count = 300;
	};

	struct EventSpecManager
	{
		struct Config
		{
			std::size_t max_space_id_count = 8000;
			bool need_file = true;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		EventSpecManager(Config config = {})
			: string_storage(config.resource), events(config.resource), out_range_spec(config.resource), max_space_count(config.max_space_id_count), need_file(config.need_file)
		{

		}

		EventSpecManager(EventSpecManager&&) = default;
		EventSpecManager(EventSpecManager const&) = default;

		struct EventSpecView
		{
			EventID id;
			std::wstring_view name_view = L"UnknowSpecId";
			std::wstring_view file_view;
			std::size_t line = 0;
			operator bool() const { return id; }
		};

		EventSpecView GetEventSpec(EventID id) const;

		bool AddEvent(EventID id, std::wstring_view event_name, std::wstring_view file, std::size_t line);

	protected:

		struct Spec
		{
			EventID id;
			Potato::Misc::IndexSpan<> name_index;
			std::wstring_view debug_name_view;
			Potato::Misc::IndexSpan<> file_index;
			std::wstring_view debug_file;
			std::size_t line;
		};

		const bool need_file;
		const std::size_t max_space_count;
		std::pmr::wstring string_storage;

		std::pmr::vector<Spec> events;
		std::pmr::unordered_map<EventID, Spec> out_range_spec;

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
		virtual bool IsParserRequire(ParserRequireFlag flag) const override { return true; }
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) {}
		virtual void OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) {}
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) {}
		virtual void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) {}
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
		
		virtual void OnCPUEventDiscoverd(uint32_t space_id, wchar_t const* event_name, std::size_t event_name_len, wchar_t const* file, std::size_t file_name_len, std::size_t line) override
		{
			return OnCPUEventDiscoverd(
				EventID{ space_id }, std::wstring_view{ event_name, event_name_len }, std::wstring_view{ file, file_name_len }, line
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

		virtual void OnThreadDiscoverd(uint32 thread_id, uint32 thread_system_id, char const* thread_name, std::size_t thread_name_len) override;
		
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