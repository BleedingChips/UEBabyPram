module;
#include <cassert>
#include <cmath>
#include <cstdio>
#include <re2/re2.h>
module UEBabyPramInsightFilterEventStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserContextSwitchStreamer;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// The pattern of a matcher in wide characters, the report is printed in wide characters.
		std::wstring EncodePattern(re2::RE2 const& matcher)
		{
			std::wstring pattern;
			Potato::Encode::UnicodeEncoder<char, wchar_t>::EncodeTo(
				std::string_view{ matcher.pattern() },
				std::back_inserter(pattern)
			);
			return pattern;
		}

		// Whether the two time ranges have a common time, the ranges which only touch each other at their
		// end are not overlapped.
		bool IsTimeRangeOverlapped(Potato::Misc::IndexSpan<DurationSec> left, Potato::Misc::IndexSpan<DurationSec> right)
		{
			switch (left <=> right)
			{
			case Potato::Misc::IndexSpanOrdering::LeftOverlapBegin:
			case Potato::Misc::IndexSpanOrdering::RightOverlapEnd:
			case Potato::Misc::IndexSpanOrdering::Contained:
			case Potato::Misc::IndexSpanOrdering::Contains:
			case Potato::Misc::IndexSpanOrdering::Equal:
				return true;
			default:
				return false;
			}
		}

		// One match of the query system which survives the collapsing of the identical calls.
		struct MatchedOccurrence
		{
			ThreadCPUEventView::EventView event_view;
			// The name of the matched event, it points into the string blocks of the events manager, which are
			// never moved while the analysis is running, so the view is valid for the whole call.
			std::u8string_view event_name;
			std::size_t query_index = 0;
		};
	}

	EventStatisticParser::EventStatisticParser(Config in_config)
		: resource(in_config.resource)
		, context_switch_streamer(ContextSwitchStreamer::Config{ 3, DurationSec{ 1 }, &resource })
		, events_manager(EventSpecManager::Config{ false, &resource })
		, query_mapping(EventQueryMapping::Config{ std::move(in_config.queries), &resource })
		, event_statistics(&resource)
		, top_count(in_config.top_count)
		, max_context_depth(in_config.max_context_depth)
	{
		// The queries are built and checked by the creator of the config, so the parser only takes the
		// ones which are ready to be analyzed.
		auto queries = query_mapping.GetQueries();
		assert(!queries.empty());
	}

	void EventStatisticParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		// The thread condition of every query is resolved from the name of the thread once, so the walk
		// of the stack tree only looks the thread of the tree up in the query which it belongs to, and
		// IsThreadRequired() is answered from the same ids.
		query_mapping.AddThread(thread_id, thread_system_id, thread_name);
	}

	void EventStatisticParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// Every event spec is stored, the name of an event is resolved from the manager when its
		// statistic is reported, and the events which only complete a call chain are resolved by the
		// query system, which needs the name of every event as well.
		// The return value of AddEvent tells whether this is the first time the event is stored, so the
		// name of an event spec is only resolved once.
		if (events_manager.AddEvent(id, event_name, file_name, file_line))
		{
			query_mapping.AddEvent(id, event_name);
		}
	}

	void EventStatisticParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, its name is the only
		// information of the spec, but it is carried by the stack tree as well, so it is resolved the
		// same way as the event of the event spec. The name is already UTF-8, which is what the matcher
		// needs, so it is resolved in place without any conversion.
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			query_mapping.AddEvent(id, event_name);
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

	bool EventStatisticParser::IsThreadRequired(ThreadID thread_id) const
	{
		// A query which carries no thread condition counts every thread, so this is only false when every
		// query carries a thread condition which the thread is not matched by, and such a thread carries
		// no event which has to be counted.
		return query_mapping.IsThreadMatched(thread_id);
	}

	bool EventStatisticParser::IsThreadRequired(ThreadSystemID thread_system_id) const
	{
		// The thread is known by its system thread id as well, which is what the events carry which are not carried
		// by the stack tree, ex: the context switch event, so the id of the thread is looked up by it first, and a
		// thread which is not discovered yet carries no info which the query could be matched with.
		auto thread_info = GetThreadInfo(thread_system_id);
		if (!thread_info)
		{
			return false;
		}

		// The thread of the stack tree is resolved by the id of the thread, so the rest of the answer is the same as
		// the answer of the thread id.
		return IsThreadRequired(thread_info->thread_id);
	}

	void EventStatisticParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The events are streamed to the listeners of the top occurrences, so an occurrence reads the time which its
		// thread spent out of the CPU from the blocks which its listener collected.
		context_switch_streamer.AddContextSwitchEvent(thread_id, core_name, duration);
	}

	void EventStatisticParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		// The query system has already filtered the thread, the time and the whole call chain of every
		// query, so every match which it visits is an occurrence which the query counts, and the
		// statistics of the match only have to be accumulated.
		//
		// Two matches of one query may be the same call, though: an event name may appear in more than one
		// chain element, and the elements of a chain do not have to be the nearest descendants, so the
		// same name can be reported by several elements with overlapping time ranges. The matches of one
		// query are collected first, and the ones whose time ranges overlap are collapsed into the longest
		// one, the shorter ones are the same call seen from another element. The matches of two queries
		// are never compared, so the same name is still counted independently by both of them.
		std::pmr::vector<MatchedOccurrence> occurrences(&resource);

		query_mapping.ForEachMatchedEvent(event_scope, [&](EventQueryMapping::EventMatch const& match) {
			std::u8string_view event_name = events_manager.GetEventSpec(match.event_view.event_id).name_view;

			for (std::size_t index = 0; index < occurrences.size();)
			{
				auto const& exist = occurrences[index];
				if (
					exist.query_index == match.query_index
					&& exist.event_name == event_name
					&& IsTimeRangeOverlapped(exist.event_view.time_range, match.event_view.time_range)
					)
				{
					// The longer range is the one which is counted, the shorter one is the same call and it
					// is ignored. The occurrence which is kept is replaced when the new one is longer, and
					// the new one is not counted at all when a longer one is already kept.
					if (exist.event_view.time_range.Size() >= match.event_view.time_range.Size())
					{
						return;
					}

					occurrences.erase(occurrences.begin() + index);
					continue;
				}

				++index;
			}

			occurrences.push_back(MatchedOccurrence{ match.event_view, event_name, match.query_index });
		});

		// The occurrences which are left are the calls which have to be counted, the exclude time of an
		// occurrence is the duration of its subtree without the time of its child events.
		for (auto const& occurrence : occurrences)
		{
			DurationSec exclude_time = DurationSec::zero();
			if (auto exclude_statistic = event_scope.GetExcludeTime(occurrence.event_view); exclude_statistic)
			{
				exclude_time = exclude_statistic->active_time;
			}

			RecordEventOccurrence(
				event_scope,
				occurrence.event_view,
				exclude_time,
				GetNameStatistic(occurrence.query_index, occurrence.event_view.event_id)
			);
		}
	}

	EventStatisticParser::EventStatistic& EventStatisticParser::GetNameStatistic(std::size_t query_index, EventID id)
	{
		assert(query_index < query_mapping.GetQueries().size());
		std::u8string_view event_name = events_manager.GetEventSpec(id).name_view;

		// The statistic is keyed by the query and by the name of the event: one name keeps one statistic
		// inside one query, and two queries never share a statistic.
		for (auto& statistic : event_statistics)
		{
			if (statistic.query_index != query_index)
			{
				continue;
			}

			assert(!statistic.event_ids.empty());
			auto current_name = events_manager.GetEventSpec(statistic.event_ids.front()).name_view;
			if (event_name == current_name)
			{
				// The same id is counted by every one of its occurrences, so it is only kept once.
				if (std::find(statistic.event_ids.begin(), statistic.event_ids.end(), id) == statistic.event_ids.end())
				{
					statistic.event_ids.push_back(id);
				}
				return statistic;
			}
		}

		// A new statistic is only created for a name which the query does not count yet, so every name
		// which the query matches keeps its own timing.
		auto& statistic = event_statistics.emplace_back(&resource);
		statistic.query_index = query_index;
		statistic.event_ids.push_back(id);
		return statistic;
	}

	void EventStatisticParser::RecordEventOccurrence(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, DurationSec exclude_time, EventStatistic& statistic)
	{
		// The duration of the occurrence is the whole time which it takes.
		DurationSec duration = event_view.time_range.Size();

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
		auto& occurrence = statistic.top_occurrences.emplace_back(&resource);
		occurrence.time_range = event_view.time_range;
		occurrence.depth = event_view.depth;
		occurrence.exclude_time = exclude_time;
		occurrence.exclude_time_without_context_switch = exclude_time;
		occurrence.thread_id = event_scope.thread_id;

		// The listener of the context switch events of the occurrence is created with the time range of the stack
		// tree which the occurrence is in, so the events of the thread which are in the range of the tree are
		// collected for the occurrence, and the occurrence reads the time which its thread spent out of the CPU from
		// the blocks which the listener collected.
		if (auto thread_info = GetThreadInfo(event_scope.thread_id); thread_info)
		{
			if (auto stack_tree_time_range = event_scope.GetTimeRange(); stack_tree_time_range)
			{
				occurrence.context_switch_listener = context_switch_streamer.AddListener(
					thread_info->thread_system_id,
					*stack_tree_time_range
				);
			}
		}

		// The occurrence is kicked out right away when it is the shortest one of a full list, and the
		// time of its own children and parents is only collected after it is confirmed to be kept, so an
		// occurrence which does not rank in the top N never walks its call context.
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
				bool is_new_occurrence = shortest == std::prev(statistic.top_occurrences.end());
				statistic.top_occurrences.erase(shortest);
				if (is_new_occurrence)
				{
					return;
				}
			}
		}

		// The occurrence is kept in the top N of the statistic, so the time of the events of its call
		// context is collected as well. The container may have been shifted by the erase above, so the
		// occurrence is taken from the back of the list again.
		auto& recorded_occurrence = statistic.top_occurrences.back();

		// The ancestors of the occurrence are collected from the direct parent outwards, but only the
		// ones which are not deeper than the depth limit, so the list is reversed after the walk to keep
		// the call order, the outermost kept ancestor is the first one.
		std::size_t parent_depth = 1;
		auto parent_view = event_scope.FindParentEvent({}, event_view);
		while (parent_view && parent_depth <= max_context_depth)
		{
			DurationSec parent_exclude_time = DurationSec::zero();
			if (auto parent_statis = event_scope.GetExcludeTime(parent_view); parent_statis)
			{
				parent_exclude_time = parent_statis->active_time;
			}
			recorded_occurrence.parent_events.push_back(ContextEvent{
				.event_id = parent_view.event_id,
				.duration = parent_view.time_range.Size(),
				.exclude_time = parent_exclude_time,
				.time_range = parent_view.time_range,
				.depth = parent_view.depth
			});
			parent_view = event_scope.FindParentEvent({}, parent_view);
			++parent_depth;
		}
		std::reverse(recorded_occurrence.parent_events.begin(), recorded_occurrence.parent_events.end());

		// The descendants of the occurrence are collected in the call order: the subtree of every recorded
		// node is entered as well, so every child of a node which has several children is recorded. Only
		// the descendants which are not deeper than the depth limit are kept; a node which is already at
		// the limit keeps a placeholder for its children instead of them, and its subtree is not entered,
		// so the events beyond the limit are neither recorded nor reported.
		if (max_context_depth == 0)
		{
			auto child_view = event_scope.FindNextEvent({}, { event_view.index_range.Begin() + 1, event_view.index_range.End() });
			if (child_view)
			{
				recorded_occurrence.child_events.push_back(ContextEvent{
					.depth = event_view.depth + 1,
					.is_ellipsis = true
				});
			}
		}
		else
		{
			std::size_t descendant_offset = event_view.index_range.Begin() + 1;
			while (descendant_offset < event_view.index_range.End())
			{
				auto descendant_view = event_scope.FindNextEvent({}, { descendant_offset, event_view.index_range.End() });
				if (!descendant_view)
				{
					break;
				}

				assert(descendant_view.depth > event_view.depth);
				std::size_t depth_offset = descendant_view.depth - event_view.depth;
				if (depth_offset > max_context_depth)
				{
					// The whole subtree of the event is deeper than the limit, so it is skipped.
					descendant_offset = descendant_view.index_range.End();
					continue;
				}

				DurationSec descendant_exclude_time = DurationSec::zero();
				if (auto descendant_statis = event_scope.GetExcludeTime(descendant_view); descendant_statis)
				{
					descendant_exclude_time = descendant_statis->active_time;
				}
				recorded_occurrence.child_events.push_back(ContextEvent{
					.event_id = descendant_view.event_id,
					.duration = descendant_view.time_range.Size(),
					.exclude_time = descendant_exclude_time,
					.time_range = descendant_view.time_range,
					.depth = descendant_view.depth
				});

				if (depth_offset == max_context_depth)
				{
					// The children of the event are beyond the limit, the placeholder takes their place
					// and the subtree is not entered.
					auto child_view = event_scope.FindNextEvent({}, { descendant_view.index_range.Begin() + 1, descendant_view.index_range.End() });
					if (child_view)
					{
						recorded_occurrence.child_events.push_back(ContextEvent{
							.depth = descendant_view.depth + 1,
							.is_ellipsis = true
						});
					}
					descendant_offset = descendant_view.index_range.End();
				}
				else
				{
					descendant_offset = descendant_view.index_range.Begin() + 1;
				}
			}
		}
	}

	void EventStatisticParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// The whole trace is analyzed, so the block which is still being written by the streamer is completed and
		// given to the listeners of the occurrences, which is what makes the listeners carry the last events of the
		// trace as well when their statistic is read below.
		context_switch_streamer.Flush();

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
			// the CPU in the whole time range of the occurrence is kept as the context switch time of
			// the occurrence and removed from its exclude time, which the occurrence is initialized
			// with. The time is read from the listener which the occurrence collected its blocks with, and
			// a range which carries no collected context switch event is treated as active in the whole
			// range by GetContextSwitchStatis(), so its context switch time stays zero and its exclude
			// time stays.
			for (auto& occurrence : statistic.top_occurrences)
			{
				auto get_context_switch_statis = [&](Potato::Misc::IndexSpan<DurationSec> time_range)
				{
					// The listener of the occurrence carries the context switch events of the thread which are in the
					// range of its stack tree, and an occurrence which has no listener carries no event of the thread,
					// so the whole range is its active time.
					if (occurrence.context_switch_listener)
					{
						return occurrence.context_switch_listener->GetContextSwitchStatis(time_range);
					}

					return ContextSwitchStreamer::Listener::Statis{ time_range.Size(), 0 };
				};

				auto context_switch_statis = get_context_switch_statis(occurrence.time_range);
				DurationSec switched_out_time = occurrence.time_range.Size() - context_switch_statis.removed_context_switch_time;
				occurrence.context_switch_time = switched_out_time;
				occurrence.exclude_time_without_context_switch = std::max(
					DurationSec::zero(),
					occurrence.exclude_time_without_context_switch - switched_out_time
				);

				// The context switch time of every event of the call context is resolved the same way,
				// the placeholders carry no time range and they are skipped.
				auto resolve_context_switch = [&](ContextEvent& context_event)
				{
					if (context_event.is_ellipsis)
					{
						return;
					}
					auto context_event_statis = get_context_switch_statis(context_event.time_range);
					context_event.context_switch_time = context_event.time_range.Size() - context_event_statis.removed_context_switch_time;
				};
				for (auto& parent_event : occurrence.parent_events)
				{
					resolve_context_switch(parent_event);
				}
				for (auto& child_event : occurrence.child_events)
				{
					resolve_context_switch(child_event);
				}
			}

			// Report the longest occurrence of the top list first, the shortest one is the last, the
			// occurrence which did not rank in the top N is not in the list at all.
			std::sort(statistic.top_occurrences.begin(), statistic.top_occurrences.end(), [](TopEventOccurrence const& left, TopEventOccurrence const& right) {
				return left.time_range.Size() > right.time_range.Size();
				});
		}
	}

	void EventStatisticParser::PrintEventStatistic(Potato::Log::LogPrinter& printer, EventStatistic const& statistic) const
	{
		using InsightFilter::OutputLog;

		// The statistic which has no occurrence carries no timing information, so it is not reported.
		if (statistic.count == 0)
		{
			return;
		}

		// The name of the event is not stored by the statistic, it is taken from the events manager with
		// the first id of the statistic.
		std::u8string_view event_name = u8"<Unknow>";
		if (!statistic.event_ids.empty())
		{
			auto event_spec = events_manager.GetEventSpec(statistic.event_ids.front());
			if (!event_spec.name_view.empty())
			{
				event_name = event_spec.name_view;
			}
		}

		// The statistic is only reported when it has at least one occurrence, so the count is never zero
		// here.
		double mean_duration = (statistic.total_duration / static_cast<double>(statistic.count)).count() * 1000.0;
		double mean_exclude_time = (statistic.total_exclude_time / static_cast<double>(statistic.count)).count() * 1000.0;

		OutputLog<L"            Name:<{:}>, Count:<{}>, Total D:<{:.3f}ms>, Mean D:<{:.3f}ms>, Median D:<{:.3f}ms>, Std D:<{:.3f}ms>, Total E:<{:.3f}ms>, Mean E:<{:.3f}ms>">(
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
			return;
		}

		OutputLog<L"                Top <{}> Longest Occurrence:">(printer, statistic.top_occurrences.size());

		std::size_t index = 0;
		for (auto const& occurrence : statistic.top_occurrences)
		{
			DisplayTimePoint begin_time{ occurrence.time_range.Begin() };
			DisplayTimePoint end_time{ occurrence.time_range.End() };

			// The name of the thread is not kept by the occurrence, it is resolved from the thread id
			// which is kept at the time of the occurrence. The thread info may be missing when the thread
			// was never discovered.
			std::string_view thread_name{ "<Unknow>" };
			if (auto thread_info = GetThreadInfo(occurrence.thread_id); thread_info && !thread_info->thread_name.empty())
			{
				thread_name = thread_info->thread_name;
			}

			OutputLog<L"                    <{:<4}> TName:<{:}>, D:<{:.3f}ms>, E:<{:.3f}ms>, ENCS:<{:.3f}ms>, CS:<{:.3f}ms>, TR: [{}, {}]">(
				printer,
				index,
				thread_name,
				occurrence.time_range.Size().count() * 1000.0,
				occurrence.exclude_time.count() * 1000.0,
				occurrence.exclude_time_without_context_switch.count() * 1000.0,
				occurrence.context_switch_time.count() * 1000.0,
				begin_time,
				end_time
			);

			++index;
		}

		// The call context of every one of the top occurrences is reported after the top list as a call
		// tree which is drawn with box drawing characters. The parents are the levels above the
		// occurrence and the children are the levels below it, the occurrence itself is the last level
		// of the parents and the first level of the children, and it is marked with '***[...]***', the
		// other events are marked with '[...]' inside '<>'. The levels are connected with the box
		// drawing characters (U+251C, U+2514, U+2500 and U+2502), and a '...' line stands for the
		// levels which are omitted by the depth limit.
		std::size_t occurrence_index = 0;
		for (auto const& occurrence : statistic.top_occurrences)
		{
			auto resolve_name = [&](EventID id) -> std::u8string_view {
				if (!id)
				{
					return u8"<Unknow>";
				}
				auto event_spec = events_manager.GetEventSpec(id);
				return event_spec.name_view.empty() ? std::u8string_view{ u8"<Unknow>" } : event_spec.name_view;
			};

			DisplayTimePoint begin_time{ occurrence.time_range.Begin() };
			DisplayTimePoint end_time{ occurrence.time_range.End() };
			OutputLog<L"                Event <{}> Call Context: TR: [{}, {}]">(printer, occurrence_index, begin_time, end_time);
			OutputLog<L"                Parents:">(printer);

			// The ancestors which are above the first kept parent are omitted when the depth limit cuts
			// the chain, the ellipsis takes their place.
			bool has_omitted_parent = occurrence.parent_events.empty()
				? occurrence.depth != 0
				: occurrence.parent_events.front().depth != 0;
			if (has_omitted_parent)
			{
				OutputLog<L"                    ...">(printer);
			}

			// The parents are stored from the outermost kept ancestor to the direct parent, so every
			// level is the only child of the level above it, the current event is the last level.
			std::wstring parent_prefix;
			bool first_parent = true;
			for (auto const& parent : occurrence.parent_events)
			{
				std::wstring line_prefix;
				if (!first_parent)
				{
					line_prefix = parent_prefix + L"\u2514\u2500 ";
				}

				OutputLog<L"                    {}<{}>, D:<{:.3f}ms>, E:<{:.3f}ms>, CS:<{:.3f}ms>">(
					printer,
					line_prefix,
					resolve_name(parent.event_id),
					parent.duration.count() * 1000.0,
					parent.exclude_time.count() * 1000.0,
					parent.context_switch_time.count() * 1000.0
				);

				if (!first_parent)
				{
					parent_prefix += L"   ";
				}
				first_parent = false;
			}

			std::wstring occurrence_prefix;
			if (!occurrence.parent_events.empty())
			{
				occurrence_prefix = parent_prefix + L"\u2514\u2500 ";
			}

			OutputLog<L"                    {}***[{}]***, D:<{:.3f}ms>, E:<{:.3f}ms>, CS:<{:.3f}ms>">(
				printer,
				occurrence_prefix,
				event_name,
				occurrence.time_range.Size().count() * 1000.0,
				occurrence.exclude_time.count() * 1000.0,
				occurrence.context_switch_time.count() * 1000.0
			);

			OutputLog<L"                Children:">(printer);

			// The first level of the children is the current event itself, it is the root of the
			// children tree and it is printed without any connector.
			OutputLog<L"                    ***[{}]***, D:<{:.3f}ms>, E:<{:.3f}ms>, CS:<{:.3f}ms>">(
				printer,
				event_name,
				occurrence.time_range.Size().count() * 1000.0,
				occurrence.exclude_time.count() * 1000.0,
				occurrence.context_switch_time.count() * 1000.0
			);

			// The children are stored in the call order, so the last child of every level is found by
			// looking for the next sibling before the next level which is above it. The levels which
			// have a next sibling are followed by a vertical bar, the others by spaces.
			std::vector<bool> sibling_is_last;
			for (std::size_t child_index = 0; child_index < occurrence.child_events.size(); ++child_index)
			{
				auto const& child = occurrence.child_events[child_index];
				std::size_t depth = child.depth - occurrence.depth;

				bool is_last = true;
				for (std::size_t next_index = child_index + 1; next_index < occurrence.child_events.size(); ++next_index)
				{
					std::size_t next_depth = occurrence.child_events[next_index].depth - occurrence.depth;
					if (next_depth < depth)
					{
						break;
					}
					if (next_depth == depth)
					{
						is_last = false;
						break;
					}
				}

				if (sibling_is_last.size() <= depth)
				{
					sibling_is_last.resize(depth + 1, false);
				}
				sibling_is_last[depth] = is_last;

				std::wstring line_prefix;
				for (std::size_t level = 1; level < depth; ++level)
				{
					line_prefix += sibling_is_last[level] ? L"   " : L"\u2502  ";
				}
				line_prefix += is_last ? L"\u2514\u2500 " : L"\u251C\u2500 ";

				if (child.is_ellipsis)
				{
					OutputLog<L"                    {}...">(printer, line_prefix);
					continue;
				}

				OutputLog<L"                    {}<{}>, D:<{:.3f}ms>, E:<{:.3f}ms>, CS:<{:.3f}ms>">(
					printer,
					line_prefix,
					resolve_name(child.event_id),
					child.duration.count() * 1000.0,
					child.exclude_time.count() * 1000.0,
					child.context_switch_time.count() * 1000.0
				);
			}

			++occurrence_index;
		}
	}

	bool EventStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventStatisticParser Output:">(printer);

		OutputLog<L"    (D: Duration of the single occurrence; E: Exclude Time, the duration without the time of the child events; ENCS: Exclude Time WithOut Context Switch Time; CS: Context Switch Time, the time which the thread spent out of the CPU in the time range of the occurrence; Mean D / Median D / Std D: the mean, the median and the standard deviation of the single duration; TName: the name of the thread which the occurrence happened on; TR: Time Range of the occurrence)">(
			printer
		);

		OutputLog<L"    (The call context of every top occurrence is reported after the top list, every one of them is introduced with its index and time range: the parents are the levels above the occurrence and the last one of them is the occurrence itself, the children section starts with the occurrence itself and the levels below it follow, the occurrence which is being reported is wrapped with '***[...]***' and the other events are wrapped with '<...>', the levels are connected with box drawing characters, a '...' stands for the levels which the depth limit omits, and D, E and CS are the ones of every event in that occurrence)">(
			printer
		);

		auto queries = query_mapping.GetQueries();
		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto const& query = queries[query_index];

			// The query is printed back from the regular expressions of its elements, the elements which
			// the '[' and ']' surround are the ones whose timing is collected, the ones which the '(' and
			// ')' surround are only matched as a part of the call chain.
			std::wstring header = std::format(L"    Query <{}>:", query_index);
			for (std::size_t node_index = 0; node_index < query.nodes.size(); ++node_index)
			{
				auto const& node = query.nodes[node_index];
				header += node_index == 0 ? L" " : L" -> ";

				bool is_statistic = node.kind == EventQuery::ChainElement::Kind::QueryAndStatistic;
				header += is_statistic ? L"[" : L"(";
				if (node.matcher)
				{
					header += EncodePattern(*node.matcher);
				}
				else {
					// The element matches any event, it carries no regular expression.
					header += L"%";
				}
				header += is_statistic ? L"]" : L")";
			}

			if (query.thread_matcher)
			{
				header += L" & Thread=(";
				header += EncodePattern(*query.thread_matcher);
				header += L")";
			}

			if (!query.time_ranges.empty())
			{
				header += L" & Time=";
				for (std::size_t range_index = 0; range_index < query.time_ranges.size(); ++range_index)
				{
					if (range_index != 0)
					{
						header += L", ";
					}
					auto const& range = query.time_ranges[range_index];
					header += std::format(
						L"[{}, {}]",
						DisplayTimePoint{ range.Begin() },
						DisplayTimePoint{ range.End() }
					);
				}
			}

			OutputLog<L"{}">(printer, header);

			// The statistics which belong to this query, every one of them is keyed by the query and by the
			// name of the event, so the statistics of two queries are never mixed.
			std::size_t counted_name_count = 0;
			for (auto const& statistic : event_statistics)
			{
				if (statistic.query_index == query_index && statistic.count != 0)
				{
					++counted_name_count;
				}
			}

			if (counted_name_count == 0)
			{
				continue;
			}

			OutputLog<L"        <{}> Event Name(s):">(printer, counted_name_count);

			for (auto const& statistic : event_statistics)
			{
				if (statistic.query_index == query_index)
				{
					PrintEventStatistic(printer, statistic);
				}
			}
		}

		return true;
	}
}
