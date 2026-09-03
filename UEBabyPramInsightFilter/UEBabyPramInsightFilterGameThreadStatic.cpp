module;

module UEBabyPramInsightFilterGameStatic;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	static std::array<DurationT, 4> fps_thresholds = {
		DurationT(1.0 / 120.0),
		DurationT(1.0 / 60.0),
		DurationT(1.0 / 30.0),
		DurationT(1.0 / 15.0)
	};

	GameThreadStatic::GameThreadStatic()
	{
		
	}

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
			
			{
				std::size_t record_count = 0;
				for (; record_count < fps_thresholds.size(); ++record_count)
				{
					if (duration <= fps_thresholds[record_count])
						break;
				}
				fps_frame_record[record_count] += 1;
			}
			
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
		Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display,
			"GameThreadStatic Output:"
		>(printer);

		Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display,
			"\tTotal GameThread Time: <{}s>, Total GameFram :<{}>, Avg GameThread Time: <{}s>"
		>(
			printer,
			total_time.count(),
			total_count,
			total_time.count() / total_count
		);

		Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display,
			"\tFps: [{:.2f}%]>=120Fps, [{:.2f}%]>=60Fps, [{:.2f}%]>=30Fps, [{:.2f}%]>=15Fps, [{:.2f}%]<15FPS "
		>(
			printer,
			fps_frame_record[0] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[1] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[2] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[3] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[4] / static_cast<double>(total_count) * 100.0
		);

		Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display,
			"\tTop <{}> GameThread :"
		>(printer, max_record_frame);

		std::size_t count = 0;
		for (auto& ite : event_records)
		{
			++count;
			ThreadCPUEventView view;
			view.view = std::span(ite.event_ids.data(), ite.event_ids.size());
			auto range = *view.GetTimeRange();
			Potato::Log::LogTo<OutputCategory, Potato::Log::LogLevel::Display,
				"\t  {}. \tFrameIndex:<{}>, \tTotalDuration:<{}us>, \tTimeRange: [{:.7f}s, {:.7f}s]"
			>(printer, 
				count, 
				ite.frame_index,
				std::chrono::duration_cast<std::chrono::microseconds>(ite.duration).count(),
				ite.event_ids.begin()->time.count(),
				ite.event_ids.rbegin()->time.count()
			);
		}
	}

	void GameThreadStatic::ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<DurationT> duration)
	{
		
	}
}