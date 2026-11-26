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
		, frame_summaries(&resource)
		, csv_path(std::move(in_config.csv_path))
		, max_record_frame(in_config.max_record_frame)
		, record_buffer_limit(GetTopNBufferLimit(in_config.max_record_frame))
		, min_record_duration(DurationSec::zero())
		, max_exclude_time_count(in_config.max_exclude_time_count)
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

	bool GameThreadParser::IsParserRequired(ParserRequireFlag flag) const
	{
		return flag == ParserRequireFlag::CPU_EVENT
			|| flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::CONTEXT_SWITCH
			|| flag == ParserRequireFlag::META_DATA;
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
				// Only a stack tree whose FindNextEvent finds the tick event is a frame. Every frame is counted
				// here, and the longest ones are kept sorted, so the sorting starts while the trace is analyzed.
				auto current_time_range = *event_scope.GetTimeRange();
				DurationSec duration = current_time_range.Size();

				if (total_count == 0)
				{
					total_range = current_time_range;
				}
				else {
					total_range = Potato::Misc::IndexSpan<DurationSec>{
						std::min(total_range.Begin(), current_time_range.Begin()),
						std::max(total_range.End(), current_time_range.End())
					};
				}

				++total_count;
				total_time += duration;

				for (std::size_t i = 0; i < fps_frame_record.size(); ++i)
				{
					if (i + 1 == fps_frame_record.size() || duration < fps_thresholds[i])
					{
						fps_frame_record[i] += 1;
						break;
					}
				}

				// The listener of the frame collects the context switch events of its time range. It is shared by
				// the frame record and the CSV summary, and a frame which is dropped keeps it only when the CSV
				// file carries the frame.
				auto listener = context_switch_streamer.AddListener(game_thread_system_id, current_time_range);
				if (csv_path)
				{
					frame_summaries.push_back(FrameSummary{ current_time_range, duration, listener });
				}

				if (max_record_frame == 0)
				{
					return;
				}

				// A frame which is shorter than the shortest buffered one can not be one of the longest frames, so it is
				// dropped without copying its events. The shortest buffered frame is recorded when the buffer is sorted,
				// and it is 0 while the buffer has never been sorted, when every frame enters.
				if (duration < min_record_duration)
				{
					return;
				}

				FrameRecord record{ &resource };
				record.frame_index = total_count;
				record.duration = duration;
				record.time_range = current_time_range;
				record.context_switch_listener = listener;
				record.events.insert(record.events.end(), event_scope.view.begin(), event_scope.view.end());

				// The frames are buffered and the buffer is only sorted when it carries more than max_record_frame plus
				// the slack, so the sort is paid once per slack frames instead of once per frame: the sort keeps the
				// longest max_record_frame frames, the ones beyond them are the shortest of the buffer and they are
				// dropped, and the shortest of the kept ones is what the frames which follow are compared with.
				event_records.push_back(std::move(record));
				if (event_records.size() > record_buffer_limit)
				{
					min_record_duration = SortAndTrimTopN(
						event_records,
						max_record_frame,
						[](FrameRecord const& item) { return item.duration; }
					);
				}
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

		// The records are only sorted when the buffer is over its limit, so the frames which were added after the last
		// sort are not in order yet: the buffer is sorted from the longest frame to the shortest one here, which is the
		// order of the report, and it is cut to max_record_frame.
		if (!event_records.empty())
		{
			SortAndTrimTopN(
				event_records,
				max_record_frame,
				[](FrameRecord const& record) { return record.duration; }
			);
		}

		if (event_records.size() > 0)
		{
			// The totals and the frame rate are accumulated while the frames are analyzed, and event_records
			// is already the sorted top list, so only the exclude time lists of the kept frames are built here.
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

			// The records are sorted from the longest exclude time to the shortest one, which is the order of the report,
			// and the ones beyond max_exclude_time_count are dropped.
			if (!longest_frame_records.empty())
			{
				SortAndTrimTopN(
					longest_frame_records,
					max_exclude_time_count,
					[](CrossFrameEventRecord const& record) { return record.durations; }
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

			// The records are sorted from the longest exclude time to the shortest one, which is the order of the report,
			// and the ones beyond max_exclude_time_count are dropped.
			if (!longest_frame_records_in_top.empty())
			{
				SortAndTrimTopN(
					longest_frame_records_in_top,
					max_exclude_time_count,
					[](CrossFrameEventRecord const& record) { return record.durations; }
				);
			}
		}
	}

	bool GameThreadParser::WriteCSV() const
	{
		assert(csv_path.has_value());

		// The file is created and its previous content is emptied, the same as the report of -o, so the CSV file always
		// carries the frames of one analysis.
		Potato::Document::DocumentWriter writer{
			*csv_path,
			Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY
		};

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the CSV file <{}>">(csv_path->generic_wstring());
			return false;
		}

		std::u8string content;
		CSVRowWriter row{ content };

		// The header names every column, the user of the file never has to look at the order of the report.
		row.Text(u8"FrameIndex");
		row.Text(u8"Begin_s");
		row.Text(u8"End_s");
		row.Text(u8"Duration_ms");
		row.Text(u8"ENCS_ms");
		row.Text(u8"FPS");
		row.EndLine();

		for (std::size_t index = 0; index < frame_summaries.size(); ++index)
		{
			auto const& frame = frame_summaries[index];

			// The context switch time of the frame is read from the listener of the frame, the same as the text report
			// reads it, so the time which the game thread was really on the CPU is the same number in both of them.
			auto context_switch_statis = frame.context_switch_listener
				? frame.context_switch_listener->GetContextSwitchStatis(frame.time_range)
				: ContextSwitchStreamer::Listener::Statis{ frame.time_range.Size(), 0 };

			// The time which the frame was really on the CPU is the time with the context switch time removed, which is
			// what the statis carries, so the column is the same number as the ENCS of the text report.
			DurationSec on_cpu_time = context_switch_statis.removed_context_switch_time;
			double frame_rate = frame.duration.count() > 0.0 ? 1.0 / frame.duration.count() : 0.0;

			row.Integer(index + 1);
			row.Seconds(frame.time_range.Begin());
			row.Seconds(frame.time_range.End());
			row.Milliseconds(frame.duration);
			row.Milliseconds(on_cpu_time);
			row.Fps(frame_rate);
			row.EndLine();
		}

		writer.Write(std::span<std::byte const>{
			reinterpret_cast<std::byte const*>(content.data()),
			content.size()
		});

		return true;
	}

	bool GameThreadParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{

		using InsightFilter::OutputLog;

		// The CSV file is written instead of the text report: every analyzed frame is one row, ordered by the time,
		// which is what the text report does not carry, because it only reports the longest frames.
		if (csv_path)
		{
			return WriteCSV();
		}

		OutputLog<L"GameThreadParser Output:">(printer);

		// The context switch time of an event is the time which the thread spent out of the CPU, so a trace which
		// carries no context switch event carries no such time, and the report tells it instead of reporting 0 as the
		// time which the thread was switched out.
		if (!context_switch_streamer.HasContextSwitchEvent())
		{
			OutputNoContextSwitchEventNote(printer);
		}

		// The totals and the frame rate cover every frame, while the longest frame list keeps the longest
		// max_record_frame frames only. The first 4 and the last 4 frames are at the beginning and at the end of
		// the recording, where the trace is saved, so their time may be inaccurate; when such a frame is in the
		// longest frame list, its recording phase and frame number are reported with it.
		OutputLog<L"    (All frames counted; Top {} keeps the longest frames; first/last 4 frames are marked when listed)">(
			printer,
			max_record_frame
		);
		OutputLog<L"    (E: Elapsed; ENCS: Elapsed without CS; TR: Time Range; OC: Occurrence Count)">(
			printer
		);

		// A trace which carries no game frame has nothing to report, and every average of the report would be a
		// division by zero.
		if (total_count == 0)
		{
			OutputLog<L"    No GameFrame is recorded, there is nothing to report.">(printer);
			return true;
		}

		double mean_frame_time_millisecond = std::chrono::duration_cast<
			std::chrono::duration<double, std::milli>
		>(total_time / total_count).count();

		OutputLog<L"    Total GameThread Time: <{:.3f}s>, Total GameFrame: <{}>, Avg GameThread Time: <{:.3f}ms>">(
			printer,
			total_time.count(),
			total_count,
			mean_frame_time_millisecond
		);

		// The frame rate is reported as a table instead of one line: every level keeps the range of the frame time which
		// belongs to it, how many frames fell into it and their share, so the level of a share is read from the row
		// which carries it. The levels and the frame time ranges are written from the same thresholds which the frames
		// are classified by, so the table and the classification never go out of sync.
		auto frame_rate_of = [](DurationSec threshold) { return 1.0 / threshold.count(); };
		auto frame_time_limit_text = [](std::wstring_view prefix, DurationSec limit) {
			return std::wstring{ prefix } + L" " + std::format(L"{:.2f} ms", limit.count() * 1000.0);
		};
		auto frame_time_range_text = [](DurationSec begin, DurationSec end) {
			return std::format(L"{:.2f} ~ {:.2f} ms", begin.count() * 1000.0, end.count() * 1000.0);
		};

		// The columns of the table are aligned by padding the cell, which is ASCII for every cell of the table.
		constexpr std::size_t level_width = 14;
		constexpr std::size_t frame_time_width = 20;
		constexpr std::size_t count_width = 8;
		constexpr std::size_t share_width = 8;

		auto pad_left = [](std::wstring_view text, std::size_t width) {
			std::wstring padded{ text };
			if (padded.size() < width)
			{
				padded.append(width - padded.size(), L' ');
			}
			return padded;
		};
		auto pad_right = [](std::wstring_view text, std::size_t width) {
			std::wstring padded;
			if (text.size() < width)
			{
				padded.append(width - text.size(), L' ');
			}
			padded.append(text);
			return padded;
		};
		auto share_of = [this](std::size_t frame_count) {
			return static_cast<double>(frame_count) / static_cast<double>(total_count) * 100.0;
		};
		auto print_frame_rate_row = [&](std::wstring_view level, std::wstring_view frame_time, std::size_t frame_count) {
			OutputLog<L"        {}  {}  {}  {}">(
				printer,
				pad_left(level, level_width),
				pad_left(frame_time, frame_time_width),
				pad_right(std::format(L"{}", frame_count), count_width),
				pad_right(std::format(L"{:.2f}%", share_of(frame_count)), share_width)
			);
		};

		double mean_frame_rate = total_time.count() > 0.0
			? static_cast<double>(total_count) / total_time.count()
			: 0.0;

		OutputLog<L"    Frame Rate: Total <{}> Frame(s), Mean Frame Time <{:.3f}ms>, Mean <{:.2f}> FPS">(
			printer,
			total_count,
			mean_frame_time_millisecond,
			mean_frame_rate
		);

		OutputLog<L"        {}  {}  {}  {}">(
			printer,
			pad_left(L"FPS Level", level_width),
			pad_left(L"Frame Time", frame_time_width),
			pad_right(L"Frames", count_width),
			pad_right(L"Share", share_width)
		);

		print_frame_rate_row(
			std::format(L"> {:.0f} FPS", frame_rate_of(fps_thresholds[0])),
			frame_time_limit_text(L"<", fps_thresholds[0]),
			fps_frame_record[0]
		);
		print_frame_rate_row(
			std::format(L"{:.0f} ~ {:.0f} FPS", frame_rate_of(fps_thresholds[1]), frame_rate_of(fps_thresholds[0])),
			frame_time_range_text(fps_thresholds[0], fps_thresholds[1]),
			fps_frame_record[1]
		);
		print_frame_rate_row(
			std::format(L"{:.0f} ~ {:.0f} FPS", frame_rate_of(fps_thresholds[2]), frame_rate_of(fps_thresholds[1])),
			frame_time_range_text(fps_thresholds[1], fps_thresholds[2]),
			fps_frame_record[2]
		);
		print_frame_rate_row(
			std::format(L"{:.0f} ~ {:.0f} FPS", frame_rate_of(fps_thresholds[3]), frame_rate_of(fps_thresholds[2])),
			frame_time_range_text(fps_thresholds[2], fps_thresholds[3]),
			fps_frame_record[3]
		);
		print_frame_rate_row(
			std::format(L"<= {:.0f} FPS", frame_rate_of(fps_thresholds[3])),
			frame_time_limit_text(L">=", fps_thresholds[3]),
			fps_frame_record[4]
		);

		// The levels are exclusive, so the share of the frames which are above a threshold is the sum of the levels
		// above it, which is the question of "did the game hold this frame rate".
		std::size_t frame_count_over_60 = fps_frame_record[0] + fps_frame_record[1];
		std::size_t frame_count_over_30 = frame_count_over_60 + fps_frame_record[2];
		std::size_t frame_count_over_15 = frame_count_over_30 + fps_frame_record[3];

		OutputLog<L"        Frames Above 60 FPS: <{}> (<{:.2f}%>), Above 30 FPS: <{}> (<{:.2f}%>), Above 15 FPS: <{}> (<{:.2f}%>)">(
			printer,
			frame_count_over_60,
			share_of(frame_count_over_60),
			frame_count_over_30,
			share_of(frame_count_over_30),
			frame_count_over_15,
			share_of(frame_count_over_15)
		);

		DisplayTimePoint total_range_display_begin{ total_range.Begin() };
		DisplayTimePoint total_range_display_end{ total_range.End() };

		OutputLog<L"    Time Range: [{}, {}]">(
			printer,
			total_range_display_begin,
			total_range_display_end
		);

		OutputLog<L"{}Top <{}> Longest GameThread:">(
			printer,
			printer.EndLineString(),
			max_record_frame
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
				// The statis carries the time of the range with the context switch time removed, which is the time which
				// the game thread was really on the CPU inside the frame, so it is the ENCS of the frame: subtracting it
				// from the frame would print the time which the thread was switched out, which is the opposite.
				std::chrono::duration_cast<
					std::chrono::duration<double, std::milli>
				>(context_switch_static.removed_context_switch_time).count(),
				start_duration,
				end_duration
			);

			// The first 4 and the last 4 frames are at the recording begin and end, where the trace is saved, so
			// their time may be inaccurate. The frame number is the number in the whole trace, not the number in
			// the sorted top list.
			constexpr std::size_t recording_phase_frame_count = 4;
			bool begin_phase = ite.frame_index <= recording_phase_frame_count;
			bool end_phase = ite.frame_index + recording_phase_frame_count > total_count;

			if (begin_phase || end_phase)
			{
				std::wstring phase;
				if (begin_phase && end_phase)
				{
					phase = L"the Recording Begin And End Phase";
				}
				else if (begin_phase)
				{
					phase = L"the Recording Begin Phase";
				}
				else {
					phase = L"the Recording End Phase";
				}

				OutputLog<L"        -- Frame <{}/{}> is in {}, the time of the frame may be inaccurate, for reference only">(
					printer,
					ite.frame_index,
					total_count,
					std::wstring_view{ phase }
				);
			}
		}

		OutputLog<L"{}Top ExcludeTime In Longgest Frame:">(
			printer,
			printer.EndLineString()
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
				// The frames of the text report are numbered from 1, so the number of the frame which the record comes
				// from is the same numbering, and not the index of the storage.
				ite.from_frame_index + 1
			);
		}
		return true;
	}

	void GameThreadParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The listeners only exist for the frames of the game thread, so an event of a thread which is not discovered
		// yet, or which is not the game thread, carries no time which is read: it is dropped before it enters the
		// streamer.
		auto thread_info = GetThreadInfo(thread_id);
		if (!thread_info || !IsThreadRequired(thread_info->thread_id))
		{
			return;
		}

		// The events are streamed to the listeners of the recorded frames, so a frame reads the time which the game
		// thread spent out of the CPU from the blocks which its listener collected.
		context_switch_streamer.AddContextSwitchEvent(thread_id, core_name, duration);
	}
}
