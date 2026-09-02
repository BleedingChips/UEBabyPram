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
			game_frame_thread_system_id = thread_system_id;
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
			auto duration = event_scope.GetTimeRange()->Size();
			
			
			if (duration > min_duration || event_records.size() < max_record_frame)
			{
				EventIDRecord records;
				records.duration = duration;
				records.frame_index = total_count;
				records.event_ids.insert(records.event_ids.end(), event_scope.view.begin(), event_scope.view.end());
				event_records.push_back(std::move(records));

				std::sort(event_records.begin(), event_records.end(), [](const EventIDRecord& a, const EventIDRecord& b) {
					return a.duration > b.duration;
					});

				if (event_records.size() > max_record_frame)
				{
					event_records.pop_back();
				}

				min_duration = event_records.rbegin()->duration;
			}
			total_count += 1;
			total_time += duration;
		}
	}

	void GameThreadStatic::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		Potato::Log::LogTo<"Output", Potato::Log::LogLevel::Display,
			"Total GameThread Time: <{}s>, Total GameFram :<{}>, Avg GameThread Time: <{}s>"
		>(
			printer,
			total_time.count(),
			total_count,
			total_time.count() / total_count
		);
	}

	void GameThreadStatic::ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<DurationT> duration)
	{
		
	}
}