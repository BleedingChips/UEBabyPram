module;

module UEBabyPramInsightFilterGameStatic;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	void GameThreadStatic::OnThreadDiscoverd(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		if (!game_frame_thread_id && thread_name == "GameThread")
		{
			game_frame_thread_id = thread_id;
		}
	}

	void GameThreadStatic::OnCPUEventDiscoverd(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		if (!tick_event_id && event_name == L"FEngineLoop::Tick")
		{
			tick_event_id = id;
		}
	}

	bool GameThreadStatic::IsThreadRequired(ThreadID thread_id) const
	{
		if (game_frame_thread_id)
		{
			return game_frame_thread_id == thread_id;
		}
		return true;
	}

	void GameThreadStatic::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (event_scope.GetTopEvent() == tick_event_id)
		{
			total_time += event_scope.GetTimeRange()->Size();
			total_count += 1;
		}
	}
}