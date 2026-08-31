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
	struct GameThreadStatic : public InsightParser::ParserInterface
	{
		void OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name);
		void OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line);
		void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		InsightParser::DurationT total_time = InsightParser::DurationT::zero();
		std::size_t total_count = 0;
	protected:
		ThreadID game_frame_thread_id;
		EventID tick_event_id;
	};
}