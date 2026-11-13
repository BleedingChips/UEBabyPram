module;
#include <cassert>
module UEBabyPramInsightFilterGameThread;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	static std::array<DurationSec, 4> fps_thresholds = {
		DurationSec(1.0 / 120.0),
		DurationSec(1.0 / 60.0),
		DurationSec(1.0 / 30.0),
		DurationSec(1.0 / 15.0)
	};

	GameThreadParser::GameThreadParser(Config in_config)
		: resource(in_config.resource)
		, context_switch_streamer(ContextSwitchStreamer::Config{ 3, DurationSec{ 1 }, &resource })
		, events_manager(EventSpecManager::Config{ false, &resource })
		, event_records(&resource)
		, max_record_frame(in_config.max_record_frame)
		, max_exclude_time_count(in_config.max_exclude_time_count)
		, sampling_ratio(in_config.sampling_ratio)
	{
		
	}

	void GameThreadParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		if (!game_thread_id && thread_name == "GameThread")
		{
			game_thread_id = thread_id;
			game_thread_system_id = thread_system_id;
		}
	}

	void GameThreadParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		if (event_name == L"FEngineLoop::Tick")
		{
			tick_event_id.push_back(id);
		}
		events_manager.AddEvent(id, event_name, file_name, file_line);
	}

	void GameThreadParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			if (event_name == u8"FEngineLoop::Tick")
			{
				tick_event_id.push_back(id);
			}
		}
	}

	bool GameThreadParser::IsThreadRequired(ThreadID thread_id) const
	{
		if (game_thread_id)
		{
			return game_thread_id == thread_id;
		}
		return true;
	}

	bool GameThreadParser::IsThreadRequired(ThreadSystemID thread_system_id) const
	{
		// The events which are not carried by the stack tree carry the system thread id of the thread, ex: the
		// context switch event, and only the game thread is analyzed.
		return game_thread_system_id && game_thread_system_id == thread_system_id;
	}

	void GameThreadParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (event_scope.thread_id == game_thread_id)
		{
			auto result = event_scope.FindNextEvent({ tick_event_id.data(), tick_event_id.size() });
			if (result)
			{
				FrameRecord records{ &resource };
				auto current_time_range = *event_scope.GetTimeRange();
				records.duration = current_time_range.Size();
				records.events.insert(records.events.end(), event_scope.view.begin(), event_scope.view.end());
				records.time_range = current_time_range;
				// The listener of the frame is created with the time range of the frame, so the context switch events
				// of the game thread which are in the frame are collected for it.
				records.context_switch_listener = context_switch_streamer.AddListener(game_thread_system_id, current_time_range);
				event_records.push_back(std::move(records));
			}
		}
	}

	void GameThreadParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// The whole trace is analyzed, so the block which is still being written by the streamer is completed and
		// given to the listeners of the frames, which is what makes the listeners carry the last events of the trace
		// as well when their statistic is read below.
		context_switch_streamer.Flush();

		if (event_records.size() > 0)
		{
			double current_sampling_ratio = std::clamp(sampling_ratio, 0.0, 1.0);
			
			total_range = Potato::Misc::IndexSpan<DurationSec>{
				event_records.front().time_range.Begin(),
				event_records.back().time_range.End()
			};
			
			sampled_range = Potato::Misc::IndexSpan<DurationSec>{
				total_range.Begin() + total_range.Size() * (1.0 - current_sampling_ratio) / 2.0,
				total_range.End() - total_range.Size() * (1.0 - current_sampling_ratio) / 2.
			};

			event_records.erase(
				std::remove_if(event_records.begin(), event_records.end(), [&](const FrameRecord& record) {
					return 	record.time_range.End() < sampled_range.Begin() || record.time_range.Begin() > sampled_range.End();
					}),
				event_records.end()
			);

			for (auto& ite : event_records)
			{
				total_time += ite.duration;
				total_count += 1;
				auto duration = ite.duration;

				for (std::size_t i = 0; i < fps_frame_record.size(); ++i)
				{
					if (i + 1 == fps_frame_record.size() || duration < fps_thresholds[i])
					{
						fps_frame_record[i] += 1;
						break;
					}
				}
			}

			std::sort(event_records.begin(), event_records.end(), [](const FrameRecord& a, const FrameRecord& b) {
				return a.duration > b.duration;
				});

			if (event_records.size() > max_record_frame)
			{
				event_records.erase(
					event_records.begin() + max_record_frame,
					event_records.end()
				);
			}

			if (event_records.size() > 0)
			{
				auto StatisRecord = [this](ThreadCPUEventView view, std::pmr::vector<CrossFrameEventRecord>& output, std::size_t frame_index) {
					// The context switch statis of a range is asked from the listener of the frame, and a frame which
					// carries no listener carries no context switch event, so the whole range is its active time.
					auto context_switch_provider = [this, frame_index](Potato::Misc::IndexSpan<DurationSec> range) {
						if (auto& listener = event_records[frame_index].context_switch_listener; listener)
						{
							auto statis = listener->GetContextSwitchStatis(range);
							return ThreadCPUEventView::ContextSwitchStatis{ statis.removed_context_switch_time, statis.switch_count };
						}
						return ThreadCPUEventView::ContextSwitchStatis{ range.Size(), 0 };
					};

					std::size_t index_offset = 0;
					while (index_offset < view.view.size())
					{
						auto current_event = view.FindNextEvent({}, index_offset);

						if (current_event)
						{
							auto event_spec = events_manager.GetEventSpec(current_event.event_id);
							auto find = std::find_if(output.begin(), output.end(), [&](CrossFrameEventRecord const& list) {
								return list.event_name == event_spec.name_view;
								});
							if (find != output.end())
							{
								auto exclude_time = *view.GetExcludeTime(current_event, context_switch_provider);
								find->durations += exclude_time.active_time;
								find->durations_without_context_switch += exclude_time.active_time_without_context_switch;
								find->count += 1;

								if (find->max_time_range.Size() < current_event.time_range.Size())
								{
									find->max_time_range = current_event.time_range;
								}
							}
							else {
								CrossFrameEventRecord new_record;
								new_record.count = 1;
								auto exclude_time = *view.GetExcludeTime(current_event, context_switch_provider);
								new_record.durations = exclude_time.active_time;
								new_record.durations_without_context_switch = exclude_time.active_time_without_context_switch;
								new_record.event_name = event_spec.name_view;
								new_record.from_frame_index = frame_index;
								new_record.max_time_range = current_event.time_range;
								output.push_back(new_record);
							}
							index_offset = current_event.index_range.Begin() + 1;
						}
						else {
							break;
						}
					}

					};

				longest_frame_records.clear();
				ThreadCPUEventView view{
					game_thread_id,
					game_thread_system_id,
					std::span(event_records.begin()->events),
					std::nullopt
				};

				StatisRecord(view, longest_frame_records, 0);

				std::sort(
					longest_frame_records.begin(),
					longest_frame_records.end(),
					[](CrossFrameEventRecord const& record1, CrossFrameEventRecord const& record2) {
						return record1.durations > record2.durations;
					}
				);

				if (longest_frame_records.size() > max_exclude_time_count)
				{
					longest_frame_records.erase(
						longest_frame_records.begin() + max_exclude_time_count,
						longest_frame_records.end()
					);
				}

				longest_frame_records_in_top.clear();

				std::pmr::vector<CrossFrameEventRecord> temp_cross_frame_record;

				for (std::size_t index = 0; index < event_records.size(); ++index)
				{
					temp_cross_frame_record.clear();
					ThreadCPUEventView view{
						game_thread_id,
						game_thread_system_id,
						std::span(event_records[index].events),
						std::nullopt
					};
					StatisRecord(view, temp_cross_frame_record, index);
					if (longest_frame_records_in_top.size() == 0)
					{
						longest_frame_records_in_top = temp_cross_frame_record;
					}
					else {
						for (auto& ite : temp_cross_frame_record)
						{
							auto finded = std::find_if(
								longest_frame_records_in_top.begin(),
								longest_frame_records_in_top.end(),
								[&](CrossFrameEventRecord const& record) {
									return record.event_name == ite.event_name;
								}
							);
							if (finded != longest_frame_records_in_top.end())
							{
								if (finded->durations > ite.durations)
								{
									ite = *finded;
								}
							}
							else {
								longest_frame_records_in_top.emplace_back(ite);
							}
						}
					}
				}

				std::sort(
					longest_frame_records_in_top.begin(),
					longest_frame_records_in_top.end(),
					[](CrossFrameEventRecord const& record1, CrossFrameEventRecord const& record2) {
						return record1.durations > record2.durations;
					}
				);

				if (longest_frame_records_in_top.size() > max_exclude_time_count)
				{
					longest_frame_records_in_top.erase(
						longest_frame_records_in_top.begin() + max_exclude_time_count,
						longest_frame_records_in_top.end()
					);
				}
			}

		}

	}

	bool GameThreadParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{

		using InsightFilter::OutputLog;

		OutputLog<L"GameThreadParser Output:">(printer);

		OutputLog<L"    Total GameThread Time: <{:.3f}s>, Total GameFrame: <{}>, Avg GameThread Time: <{:.3f}ms>">(
			printer,
			total_time.count(),
			total_count,
			std::chrono::duration_cast<
				std::chrono::duration<double, std::milli>
			>(total_time / total_count).count()
		);

		OutputLog<L"    Fps: [{:.2f}%]>=120Fps, [{:.2f}%]>=60Fps, [{:.2f}%]>=30Fps, [{:.2f}%]>=15Fps, [{:.2f}%]<15FPS">(
			printer,
			fps_frame_record[0] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[1] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[2] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[3] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[4] / static_cast<double>(total_count) * 100.0
		);

		DisplayTimePoint total_range_display_begin{ total_range.Begin() };
		DisplayTimePoint total_range_display_end{ total_range.End() };

		DisplayTimePoint sampled_range_display_begin{ sampled_range.Begin() };
		DisplayTimePoint sampled_range_display_end{ sampled_range.End() };

		OutputLog<L"    Time Range: [{}, {}] Sampled Range: [{}, {}]">(
			printer,
			total_range_display_begin,
			total_range_display_end,
			sampled_range_display_begin,
			sampled_range_display_end
		);

		OutputLog<L"{}Top <{}> Longest GameThread:">(
			printer,
			printer.EndLineString(),
			max_record_frame
		);

		OutputLog<L"    (E: Elapsed Time; ENCS: Elapsed Time WithOut Context Switch Time; TR: Time period of Longgest Elapsed Event Occurrenced)">(
			printer
		);

		std::size_t count = 0;

		for (auto& ite : event_records)
		{
			++count;
			auto range = ite.time_range;

			// The context switch time of the frame is read from the listener of the frame, a frame which carries no
			// listener carries no context switch event, so the whole range is its active time.
			auto context_switch_static = ite.context_switch_listener
				? ite.context_switch_listener->GetContextSwitchStatis(range)
				: ContextSwitchStreamer::Listener::Statis{ range.Size(), 0 };

			DisplayTimePoint start_duration{ range.Begin() };
			DisplayTimePoint end_duration{ range.End() };

			OutputLog<L"    {:<4}- E:<{:.2f}ms>, ENCS:<{:.2f}ms>, TR: [{}, {}];">(
				printer,
				count,
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.duration).count(),
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.duration - context_switch_static.removed_context_switch_time).count(),
				start_duration,
				end_duration
			);
		}

		OutputLog<L"{}Top ExcludeTime In Longgest Frame:">(
			printer,
			printer.EndLineString()
		);

		OutputLog<L"    (E: Exclude Time; ENCS: Exclude Time WithOut Context Switch Time; OC: Occurrenced Time; TR: Time period of Longgest Exclude Time Occurrenced)">(
			printer
		);

		for (auto& ite : longest_frame_records)
		{
			DisplayTimePoint m1{ ite.max_time_range.Begin() };
			DisplayTimePoint m2{ ite.max_time_range.End() };

			OutputLog< L"    Name:<{:}> -> E:<{:.2f}ms>, ENCS:<{:.2f}ms>, OC:<{:}>, TR:[{}, {}];">(
				printer,
				ite.event_name,
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.durations).count(),
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.durations_without_context_switch).count(),
				ite.count,
				m1,
				m2
			);
		}

		OutputLog<L"{}Top ExcludeTime In Top {} Longgest Frame:">(
			printer,
			printer.EndLineString(),
			max_record_frame
		);

		OutputLog<L"    (E: Exclude Time; ENCS: Exclude Time WithOut Context Switch Time; OC: Occurrenced Time; TR: Time period of Longgest Exclude Time Occurrenced)">(
			printer
		);

		for (auto& ite : longest_frame_records_in_top)
		{
			DisplayTimePoint m1{ ite.max_time_range.Begin() };
			DisplayTimePoint m2{ ite.max_time_range.End() };

			OutputLog< L"    Name:<{:}> -> E:<{:.2f}ms>, ENCS:<{:.2f}ms>, OC:<{:}>, TR:[{}, {}]. From <No.{:}> Top Frame;">(
				printer,
				ite.event_name,
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.durations).count(),
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.durations_without_context_switch).count(),
				ite.count,
				m1,
				m2,
				ite.from_frame_index
			);
		}
		return true;
	}

	void GameThreadParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The events are streamed to the listeners of the recorded frames, so a frame reads the time which the game
		// thread spent out of the CPU from the blocks which its listener collected.
		context_switch_streamer.AddContextSwitchEvent(thread_id, core_name, duration);
	}
}