module;
#include <cassert>
module UEBabyPramInsightFilterEventStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	EventStatisticParser::EventStatisticParser(Config in_config)
		: resource(in_config.resource)
		, context_switch_list(ContextSwitchEventList::Config{ &resource })
		, events_manager(EventSpecManager::Config{ 10000, 1000, false, &resource })
		, matcher(std::move(in_config.matcher))
		, event_statistics(&resource)
		, top_count(in_config.top_count)
	{
		// The matcher is compiled and checked by the creator of the config, an invalid matcher has
		// to be reported there, the parser never reports the error of the matcher.
		assert(matcher && matcher->ok());
	}

	// The name of the event is resolved from the events manager, and the statistic which already
	// carries that name is found with the name index, so every event name is grouped as soon as it
	// is discovered, and a new statistic is only created for a name which is seen for the first time.
	std::size_t EventStatisticParser::AddEventStatistic(EventID id)
	{
		std::wstring_view event_name = events_manager.GetEventSpec(id).name_view;

		for (std::size_t index = 0; index < event_statistics.size(); ++index)
		{
			auto& statistic = event_statistics[index];
			assert(!statistic.event_ids.empty());
			auto cur_name = events_manager.GetEventSpec(statistic.event_ids.front()).name_view;
			if (event_name == cur_name)
			{
				statistic.event_ids.emplace_back(id);
				return index;
			}
		}

		std::size_t index = event_statistics.size();
		event_statistics.emplace_back(&resource).event_ids.push_back(id);
		return index;
	}

	void EventStatisticParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// Every event is stored, the name of the enclosing events is needed to build the call chain
		// of the longest occurrences, those events are matched by the matcher in general.
		// The return value of AddEvent tells whether this is the first time the event is stored,
		// so the name is only checked once per event spec.
		if (events_manager.AddEvent(id, event_name, file_name, file_line))
		{
			// Keep the id of the matched event, so that the sample of every occurrence in the stack
			// tree is found by a single lookup.
			if (!event_name.empty() && IsEventNameMatched(*matcher, event_name, encoded_event_name))
			{
				AddEventStatistic(id);
			}
		}
	}

	void EventStatisticParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, its name is the only
		// information of the spec, but it is carried by the stack tree as well, so it is handled
		// the same way as the event of the event spec. The name is already UTF-8, which is what the
		// matcher needs, so it is matched in place without any conversion.
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			if (!event_name.empty() && IsEventNameMatched(*matcher, event_name))
			{
				AddEventStatistic(id);
			}
		}
	}

	bool EventStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the name of every event, the metadata carries the spec of the
		// event of the CPU scope logger, the CPU event carries the stack tree which carries the
		// timing of every event, and the context switch event carries the time which the thread
		// spent out of the CPU, which is removed from the exclude time of the occurrences.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT
			|| flag == ParserRequireFlag::CONTEXT_SWITCH;
	}

	void EventStatisticParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The context switch events are complete only when the whole trace is analyzed, so they are
		// collected here and the time without them is built from the list in AllAnalyzeDone().
		context_switch_list.AddContextSwitchEvent(thread_id, core_name, duration);
	}

	void EventStatisticParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		for (EventStatistic& ite : event_statistics)
		{
			std::size_t index_offset = 0;
			while (index_offset < event_scope.view.size())
			{
				auto current_event = FindStatisticEvent(event_scope, ite, index_offset);
				if (current_event)
				{
					RecordEventOccurrence(event_scope, current_event, ite);
					index_offset = current_event.index_range.End();
					continue;
				}
				else {
					break;
				}
			}
		}
	}

	// The search of the occurrence is the place where the rule of which event counts as an
	// occurrence of the statistic will grow, so it is kept apart from the walk of the stack tree.
	auto EventStatisticParser::FindStatisticEvent(ThreadCPUEventView event_scope, EventStatistic const& statistic, std::size_t offset) const -> ThreadCPUEventView::EventView
	{
		return event_scope.FindNextEvent(
			{ statistic.event_ids.data(), statistic.event_ids.size() },
			offset
		);
	}

	void EventStatisticParser::RecordEventOccurrence(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, EventStatistic& statistic)
	{
		// The duration of the occurrence is the whole time which it takes, and the exclude time is
		// its duration without the time spent in the child events.
		DurationSec duration = event_view.time_range.Size();
		DurationSec exclude_time = DurationSec::zero();
		if (auto exclude_statistic = event_scope.GetExcludeTime(event_view); exclude_statistic)
		{
			exclude_time = exclude_statistic->active_time;
		}

		// Accumulate the occurrence into the statistic of its event name, the duration of the single
		// occurrence is kept for the median and the standard deviation.
		++statistic.count;
		statistic.total_duration += duration;
		statistic.total_exclude_time += exclude_time;
		statistic.durations.push_back(duration);

		if (top_count == 0)
		{
			return;
		}

		// Keep the top N longest occurrences of this event name: the occurrence is recorded and the
		// shortest one is kicked out when the list is already full, so an occurrence which does not
		// rank in the top N is kicked out right away. The thread of the stack tree is kept with the
		// occurrence, its name is resolved when the occurrence is reported, and the exclude time
		// without the context switch time is initialized to the exclude time, the context switch time
		// is only removed in AllAnalyzeDone(), when the context switch events are complete.
		auto& occurrence = statistic.top_occurrences.emplace_back();
		occurrence.time_range = event_view.time_range;
		occurrence.exclude_time = exclude_time;
		occurrence.exclude_time_without_context_switch = exclude_time;
		occurrence.thread_id = event_scope.thread_id;
		if (statistic.top_occurrences.size() > top_count)
		{
			auto shortest = std::min_element(
				statistic.top_occurrences.begin(),
				statistic.top_occurrences.end(),
				[](TopEventOccurrence const& left, TopEventOccurrence const& right) {
					return left.time_range.Size() < right.time_range.Size();
				});
			if (shortest != statistic.top_occurrences.end())
			{
				statistic.top_occurrences.erase(shortest);
			}
		}
	}

	void EventStatisticParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		for (auto& statistic : event_statistics)
		{
			if (statistic.count == 0)
			{
				continue;
			}

			// The median and the standard deviation are computed from the duration of the single
			// occurrence, so the durations are sorted first.
			std::sort(statistic.durations.begin(), statistic.durations.end());

			// The median is the middle duration for an odd count, and the mean of the two middle
			// durations for an even count.
			std::size_t middle_index = statistic.count / 2;
			if ((statistic.count % 2) != 0)
			{
				statistic.median_duration = statistic.durations[middle_index];
			}
			else {
				statistic.median_duration = (statistic.durations[middle_index - 1] + statistic.durations[middle_index]) / 2.0;
			}

			// The standard deviation is the square root of the variance of the durations, the two
			// pass form keeps the precision, the mean is taken from the total which is accumulated.
			DurationSec mean_duration = statistic.total_duration / static_cast<double>(statistic.count);
			double square_distance = 0.0;
			for (auto const& duration : statistic.durations)
			{
				double distance = (duration - mean_duration).count();
				square_distance += distance * distance;
			}
			statistic.duration_standard_deviation = DurationSec{ std::sqrt(square_distance / static_cast<double>(statistic.count)) };

			// The context switch events are complete now, so the time which the thread spent out of
			// the CPU in the whole time range of the occurrence is removed from the exclude time,
			// which the occurrence is initialized with. The thread which the occurrence happened on
			// is resolved by its id, and a thread which carries no context switch event is treated
			// as active in the whole range by GetContextSwitchStatis(), so its exclude time stays.
			for (auto& occurrence : statistic.top_occurrences)
			{
				auto thread_info = GetThreadInfo(occurrence.thread_id);
				if (!thread_info)
				{
					continue;
				}

				auto context_switch_statis = context_switch_list.GetContextSwitchStatis(thread_info->thread_system_id, occurrence.time_range);
				DurationSec switched_out_time = occurrence.time_range.Size() - context_switch_statis.removed_context_switch_time;
				occurrence.exclude_time_without_context_switch = std::max(
					DurationSec::zero(),
					occurrence.exclude_time_without_context_switch - switched_out_time
				);
			}

			// Report the longest occurrence of the top list first, the shortest one is the last, the
			// occurrence which did not rank in the top N is not in the list at all.
			std::sort(statistic.top_occurrences.begin(), statistic.top_occurrences.end(), [](TopEventOccurrence const& left, TopEventOccurrence const& right) {
				return left.time_range.Size() > right.time_range.Size();
				});
		}
	}

	bool EventStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventStatisticParser Output:">(printer);

		// Every statistic is reported on its own line, so the statistics are not summed up here.
		OutputLog<L"    Event Regex <{}>">(
			printer,
			std::string_view{ matcher->pattern() }
		);

		OutputLog<L"    (D: Duration of the single occurrence; E: Exclude Time, the duration without the time of the child events; ENCS: Exclude Time WithOut Context Switch Time; Mean D / Median D / Std D: the mean, the median and the standard deviation of the single duration; TName: the name of the thread which the occurrence happened on; TR: Time Range of the occurrence)">(
			printer
		);

		for (auto const& statistic : event_statistics)
		{
			// The statistic which has no occurrence carries no timing information, so it is not
			// reported at all.
			if (statistic.count == 0)
			{
				continue;
			}

			// The name of the event is not stored by the statistic, it is taken from the events
			// manager with the first id of the statistic.
			std::wstring_view event_name = L"<Unknow>";
			if (!statistic.event_ids.empty())
			{
				auto event_spec = events_manager.GetEventSpec(statistic.event_ids.front());
				if (!event_spec.name_view.empty())
				{
					event_name = event_spec.name_view;
				}
			}

			// The statistic is only reported when it has at least one occurrence, so the count is
			// never zero here.
			double mean_duration = (statistic.total_duration / static_cast<double>(statistic.count)).count() * 1000.0;
			double mean_exclude_time = (statistic.total_exclude_time / static_cast<double>(statistic.count)).count() * 1000.0;

			OutputLog<L"    Name:<{:}>, Count:<{}>, Total D:<{:.3f}ms>, Mean D:<{:.3f}ms>, Median D:<{:.3f}ms>, Std D:<{:.3f}ms>, Total E:<{:.3f}ms>, Mean E:<{:.3f}ms>">(
				printer,
				event_name,
				statistic.count,
				statistic.total_duration.count() * 1000.0,
				mean_duration,
				statistic.median_duration.count() * 1000.0,
				statistic.duration_standard_deviation.count() * 1000.0,
				statistic.total_exclude_time.count() * 1000.0,
				mean_exclude_time
			);

			if (statistic.top_occurrences.empty())
			{
				continue;
			}

			OutputLog<L"        Top <{}> Longest Occurrence:">(printer, statistic.top_occurrences.size());

			std::size_t index = 0;
			for (auto const& occurrence : statistic.top_occurrences)
			{
				DisplayTimePoint begin_time{ occurrence.time_range.Begin() };
				DisplayTimePoint end_time{ occurrence.time_range.End() };

				// The name of the thread is not kept by the occurrence, it is resolved from the thread
				// id which is kept at the time of the occurrence. The thread info may be missing when
				// the thread was never discovered.
				std::string_view thread_name{ "<Unknow>" };
				if (auto thread_info = GetThreadInfo(occurrence.thread_id); thread_info && !thread_info->thread_name.empty())
				{
					thread_name = thread_info->thread_name;
				}

				OutputLog<L"            <{:<4}> TName:<{:}>, D:<{:.3f}ms>, E:<{:.3f}ms>, ENCS:<{:.3f}ms>, TR: [{}, {}]">(
					printer,
					index,
					thread_name,
					occurrence.time_range.Size().count() * 1000.0,
					occurrence.exclude_time.count() * 1000.0,
					occurrence.exclude_time_without_context_switch.count() * 1000.0,
					begin_time,
					end_time
				);

				++index;
			}
		}

		return true;
	}
}
