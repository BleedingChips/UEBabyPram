module;
#include <cassert>
module UEBabyPramInsightFilterGameStatic;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	static std::array<DurationSec, 4> fps_thresholds = {
		DurationSec(1.0 / 120.0),
		DurationSec(1.0 / 60.0),
		DurationSec(1.0 / 30.0),
		DurationSec(1.0 / 15.0)
	};

	GameThreadStatic::GameThreadStatic()
		: event_records(&resource), context_switch_list(ContextSwitchEventList::Config{ &resource }), events_manager(EventSpecManager::Config{10000, false, &resource})
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
		events_manager.AddEvent(id, event_name, file_name, file_line);
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
					EventIDRecord records{&resource};
					records.duration = duration;
					records.events.insert(records.events.end(), event_scope.view.begin(), event_scope.view.end());
					records.time_range = *event_scope.GetTimeRange();
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

	void GameThreadStatic::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();


	}

	bool GameThreadStatic::PrintToLog(std::pmr::wstring& out_string)
	{
		std::format_to(
			std::back_insert_iterator{ out_string },
			L"GameThreadStatic Output:\n"
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"   Total GameThread Time: <{}s>, Total GameFrame :<{}>, Avg GameThread Time: <{}s>\n",
			total_time.count(),
			total_count,
			total_time.count() / total_count
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"    Fps: [{:.2f}%]>=120Fps, [{:.2f}%]>=60Fps, [{:.2f}%]>=30Fps, [{:.2f}%]>=15Fps, [{:.2f}%]<15FPS \n",
			fps_frame_record[0] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[1] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[2] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[3] / static_cast<double>(total_count) * 100.0,
			fps_frame_record[4] / static_cast<double>(total_count) * 100.0
		);

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"    Top <{}> GameThread :\n",
			max_record_frame
		);

		std::size_t count = 0;

		for (auto& ite : event_records)
		{
			++count;
			//ThreadCPUEventView view;
			//view.view = std::span(ite.event_ids.data(), ite.event_ids.size());
			auto range = ite.time_range;

			auto context_switch_static = context_switch_list.GetContextSwitchStatis(game_frame_thread_system_id, range);

			DisplayDuration start_duration{ range.Begin() };
			DisplayDuration end_duration{ range.End() };

			std::format_to(
				std::back_insert_iterator{ out_string },
				L"      {:<4}- TotalDuration:<{:.3f}ms>, RemoveContextSwitch:<{:.3f}ms>, TimeRange: [{:}m{:.6f}s, {}m{:.6f}s]\n",
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

		count = 0;

		std::vector<EventID> stacks;

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"\n\n Detal:"
		);



		struct CrossFrameRecord
		{
			std::wstring_view event_name;
			std::size_t count = 0;
			DurationSec durations = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> max_time_range;
			std::size_t from_frame_index = 0;
		};

		std::pmr::vector<CrossFrameRecord> cross_frame_record;

		struct RecordList
		{
			EventID main_id;
			std::size_t count = 0;
			std::wstring_view event_name;
			DurationSec durations = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> max_time_range;
		};

		std::size_t top_frame_index = 0;

		std::vector<RecordList> list;

		for (auto& ite : event_records)
		{

			list.clear();

			std::size_t index_offset = 0;
			ThreadCPUEventView view{ game_frame_thread_id, game_frame_thread_system_id, std::span(ite.events) };

			while (index_offset < ite.events.size())
			{
				auto current_event = view.FindNextEvent({}, index_offset);

				if (current_event)
				{
					auto find = std::find_if(list.begin(), list.end(), [&](RecordList const& list) {
						return list.main_id == current_event.event_id;
						});
					if (find != list.end())
					{
						find->durations += view.GetExcludeTime(current_event, context_switch_list)->active_time_without_context_switch;
						find->count += 1;
						
						if (find->max_time_range.Size() < current_event.time_range.Size())
						{
							find->max_time_range = current_event.time_range;
						}
					}
					else {
						RecordList new_list{ current_event.event_id, 1,  std::wstring_view{events_manager.GetEventSpec(current_event.event_id).name_view}, view.GetExcludeTime(current_event, context_switch_list)->active_time_without_context_switch, current_event.time_range};
						list.push_back(new_list);
					}
					index_offset = current_event.index_range.Begin() + 1;
				}
				else {
					break;
				}
			}
			
			for (std::size_t iterator = 1; iterator < list.size(); ++iterator)
			{
				auto& target_ref = list[iterator];
				for (std::size_t index = 0; index < iterator; ++index)
				{
					auto& ref = list[index];
					if (ref.main_id)
					{
						if (target_ref.event_name == ref.event_name)
						{
							target_ref.main_id = {};
							ref.count += target_ref.count;
							ref.durations += target_ref.durations;
							if (ref.max_time_range.Size() < target_ref.max_time_range.Size())
							{
								ref.max_time_range = target_ref.max_time_range;
							}
							break;
						}
					}
				}
			}

			list.erase(
				std::remove_if(list.begin(), list.end(), [](RecordList const& i1) {return !i1.main_id; }),
				list.end()
			);

			for (auto& ite : list)
			{

				CrossFrameRecord new_record;
				new_record.event_name = ite.event_name;
				new_record.count = ite.count;
				new_record.durations = ite.durations;
				new_record.max_time_range = ite.max_time_range;
				new_record.from_frame_index = top_frame_index;


				auto find_ite = std::find_if(
					cross_frame_record.begin(),
					cross_frame_record.end(),
					[&](CrossFrameRecord const& record) {
						return record.event_name == ite.event_name;
					}
				);
				if (find_ite == cross_frame_record.end())
				{
					cross_frame_record.emplace_back(new_record);
				}
				else {
					if (find_ite->durations < ite.durations)
					{
						*find_ite = new_record;
					}
				}
			}

			/*
			std::sort(list.begin(), list.end(), [](RecordList const& i1, RecordList const& i2) {
				return i1.durations > i2.durations;
				});


			if (list.size() > 10)
			{
				list.resize(10);
			}

			for (auto& ite : list)
			{
				DisplayDuration m1{ite.max_time_range.Begin()};
				DisplayDuration m2{ ite.max_time_range.End() };
				std::format_to(
					std::back_insert_iterator{ out_string },
					L"  EventName:{:} Total:{:} Count:{:} TimeRange:[{:}m{:<6}s, {:}m{:<6}s]\n",
					ite.event_name,
					ite.durations.count(),
					ite.count,
					m1.minutes.count(),
					m1.seconds.count(),
					m2.minutes.count(),
					m2.seconds.count()
				);
			}
			*/


			top_frame_index += 1;
			/*
			++count;

			std::format_to(
				std::back_insert_iterator{ out_string },
				L"\n\n"
			);

			for (auto& ite2 : ite.events)
			{
				if (ite2.event_id)
				{
					for (std::size_t i = 0; i < ite2.depth; ++i)
					{
						if (ite2.depth == 0)
						{
							out_string += '+';
						}
						else {
							out_string += '-';
						}
					}
					out_string += events_name[ite2.event_id];
				}
				std::format_to(
					std::back_insert_iterator{ out_string },
					L"\n"
				);
			}
			*/
		}

		std::sort(cross_frame_record.begin(), cross_frame_record.end(), [](CrossFrameRecord const& i1, CrossFrameRecord const& i2) {
			return i1.durations > i2.durations;
			});


		if (cross_frame_record.size() > max_record_frame * 5)
		{
			cross_frame_record.resize(max_record_frame * 5);
		}

		std::format_to(
			std::back_insert_iterator{ out_string },
			L"\nDetail:\n"
		);

		for (auto& ite : cross_frame_record)
		{
			auto m1 = DisplayDuration{ ite.max_time_range.Begin() };
			auto m2 = DisplayDuration{ ite.max_time_range.End() };
			std::format_to(
				std::back_insert_iterator{ out_string },
				L"Name:<{:}> \tDuration:<{:.3}ms> Count:<{:}> LonggestTimeRange:[{:}m{:.9}s, {:}m{:.9}s] From No.<{:}> Top Frame. \n",
				ite.event_name,
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(ite.durations).count(),
				ite.count,
				m1.minutes.count(),
				m1.seconds.count(),
				m2.minutes.count(),
				m2.seconds.count(),
				ite.from_frame_index
			);
		}

		return true;
	}

	void GameThreadStatic::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		context_switch_list.AddContextSwitchEvent(thread_id, core_name, duration);
	}
}