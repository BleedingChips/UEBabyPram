module;

export module UEBabyPramInsightFilterGameStatic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{

	struct GameThreadStatic : public Parser
	{
		void OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name);
		void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line);
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const { return true; }
		virtual void AllAnalyzeDone();
		bool PrintToLog(std::pmr::wstring& out_string) override;
		GameThreadStatic();
	
	protected:

		std::pmr::unsynchronized_pool_resource resource;
		ContextSwitchEventList context_switch_list;
		EventSpecManager events_manager;

		struct EventIDRecord
		{
			EventIDRecord(std::pmr::memory_resource* resource) : events(resource) {}
			DurationSec duration = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> time_range;
			std::pmr::vector<ThreadCPUEvent> events;
		};

		std::pmr::vector<EventIDRecord> event_records;
		DurationSec min_duration = DurationSec::zero();
		std::size_t max_record_frame = 10;
		std::size_t top_event_id_count = 10;
		std::array<std::size_t, 5> fps_frame_record = {0, 0, 0, 0, 0};
		InsightParser::DurationSec total_time = InsightParser::DurationSec::zero();
		std::size_t total_count = 0;
		ThreadID game_frame_thread_id;
		ThreadSystemID game_frame_thread_system_id;
		std::vector<EventID> tick_event_id;
		
	};
}