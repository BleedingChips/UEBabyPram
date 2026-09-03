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
		void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		void ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<DurationT> duration) override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;

		void PrintToLog(Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter()) override;
		GameThreadStatic();
	protected:

		struct EventIDRecord
		{
			DurationT duration = DurationT::zero();
			std::size_t frame_index = 0;
			std::vector<ThreadCPUEvent> event_ids;
		};

		std::vector<EventIDRecord> event_records;
		DurationT min_duration = DurationT::zero();
		std::size_t max_record_frame = 10;
		std::size_t top_event_id_count = 10;
		std::array<std::size_t, 5> fps_frame_record = {0, 0, 0, 0, 0};
		InsightParser::DurationT total_time = InsightParser::DurationT::zero();
		std::size_t total_count = 0;
		ThreadID game_frame_thread_id;
		ThreadSystemID game_frame_thread_system_id;
		EventID tick_event_id;
	};
}