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
		if (event_name == L"FEngineLoop::Tick")
		{
			tick_event_id.push_back(id);
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
		if (event_scope.thread_id == game_frame_thread_id)
		{
			auto result = event_scope.FindNextEvent({ tick_event_id.data(), tick_event_id.size() });
			if (result)
			{
				auto k = event_scope.GetExcludeTime(result);
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
	}

	bool GameThreadStatic::PrintToLog(std::pmr::wstring& out_string)
	{
		std::format_to(
			std::back_insert_iterator{ out_string },
			L"GameThreadStatic Output:\n"
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"\tTotal GameThread Time: <{}s>, Total GameFram :<{}>, Avg GameThread Time: <{}s>\n",
			total_time.count(),
			total_count,
			total_time.count() / total_count
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"\tFps: [{:.2f}%]>=120Fps, [{:.2f}%]>=60Fps, [{:.2f}%]>=30Fps, [{:.2f}%]>=15Fps, [{:.2f}%]<15FPS \n",
			fps_frame_record[0] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[1] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[2] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[3] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[4] / static_cast<double>(total_count) * 100.0
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"\tTop <{}> GameThread :\n",
			max_record_frame
		);

		std::size_t count = 0;



		for (auto& ite : event_records)
		{
			++count;
			ThreadCPUEventView view;
			view.view = std::span(ite.event_ids.data(), ite.event_ids.size());
			auto range = *view.GetTimeRange();

			auto context_switch_static = context_switch_list.GetContextSwitchStatic(game_frame_thread_system_id, range);

			DisplayDuration start_duration{ range.Begin() };
			DisplayDuration end_duration{ range.End() };

			std::format_to(
				std::back_insert_iterator{ out_string },
				L"\t{:}. \tTotalDuration:<{:.3f}ms>, ContextSwitchTime:<{:.3f}ms> \tTimeRange: [{:}m{:.6f}s, {}m{:.6f}s]\n",
				count,
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.duration).count(),
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.duration - context_switch_static.active_time).count(),
				start_duration.minutes.count(),
				start_duration.seconds.count(),
				end_duration.minutes.count(),
				end_duration.seconds.count()
			);
		}

		return true;
	}

	void GameThreadStatic::ContextSwitchEvent(ThreadSystemID thread_id, uint32 core_name, Potato::Misc::IndexSpan<DurationT> duration)
	{
		context_switch_list.AddContextSwitchEvent(thread_id, core_name, duration);
	}
}