module;

export module UEBabyPramInsightFilterGameStatic;

import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	struct Parser : public InsightParser::ParserInterface
	{
		virtual void PrintToLog(Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter()) {}
	};

	struct GameThreadStatic : public Parser
	{
		void OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name);
		void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line);
		void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		void ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<DurationT> duration) override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;

		void PrintToLog(Potato::Log::LogPrinter& printer = *Potato::Log::GetLogPrinter()) override;

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

		InsightParser::DurationT total_time = InsightParser::DurationT::zero();
		std::size_t total_count = 0;
		ThreadID game_frame_thread_id;
		ThreadSystemID game_frame_thread_system_id;
		EventID tick_event_id;
	};
}