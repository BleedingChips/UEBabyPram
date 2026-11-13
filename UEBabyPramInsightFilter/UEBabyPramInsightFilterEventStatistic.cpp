module;
#include <cassert>
#include <cmath>
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

		// One match of the query system which survives the collapsing of the identical calls.
		struct MatchedOccurrence
		{
			ThreadCPUEventView::EventView event_view;
			std::size_t query_index = 0;
		};
	}

	EventStatisticParser::EventStatisticParser(Config in_config)
		: resource(in_config.resource)
		, context_switch_streamer(ContextSwitchStreamer::Config{ 3, DurationSec{ 1 }, &resource })
		, game_frame_recoder(GameFrameRecoder::Config{ &resource })
		, query_mapping(EventQueryMapping::Config{ std::move(in_config.queries), &resource })
		, event_statistics(&resource)
		, scope_event_ids(&resource)
		, top_count(in_config.top_count)
		, duration_bucket_count(in_config.duration_bucket_count)
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
		game_frame_recoder.RecordThread(thread_id, thread_system_id, thread_name);
	}

	void EventStatisticParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// The name of the event is resolved by the query system, which keeps the ids of every chain element, and the
		// statistic of the event is created for every element which exports it, with a copy of the name of the event,
		// so the report does not need the events manager. The file and the line of the spec are not used here.
		query_mapping.AddEvent(id, event_name, [this, &event_name](EventID exported_id, std::size_t query_index, std::size_t node_index) {
			// The kind of the chain element is what the statistic is keyed by: a merged element shares one statistic
			// between every event which it matches.
			EventQuery::ChainElement::Kind kind = query_mapping.GetQueries()[query_index].nodes[node_index].kind;
			AddEventStatistic(query_index, node_index, exported_id, kind, event_name);
		});

		game_frame_recoder.RecordEvent(id, event_name);
	}

	void EventStatisticParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger is reported many times with the same id, so the ids which are already
		// handled are kept and only a new id is resolved, the same as the event of the event spec. The name is
		// already UTF-8, which is what the matcher needs, so it is resolved in place without any conversion.
		if (!id)
		{
			return;
		}

		if (std::find(scope_event_ids.begin(), scope_event_ids.end(), id) != scope_event_ids.end())
		{
			return;
		}
		scope_event_ids.push_back(id);

		query_mapping.AddEvent(id, event_name, [this, &event_name](EventID exported_id, std::size_t query_index, std::size_t node_index) {
			// The kind of the chain element is what the statistic is keyed by: a merged element shares one statistic
			// between every event which it matches.
			EventQuery::ChainElement::Kind kind = query_mapping.GetQueries()[query_index].nodes[node_index].kind;
			AddEventStatistic(query_index, node_index, exported_id, kind, event_name);
		});

		game_frame_recoder.RecordEvent(id, event_name);
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
		// The game thread is analyzed even when no query counts it, because the game frames which the occurrences are
		// grouped by are the stack trees of the game thread.
		return query_mapping.IsThreadMatched(thread_id) || game_frame_recoder.IsGameThread(thread_id);
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
		// The stack tree is offered to the recorder of the game frames as well: the tree of the game thread which begins
		// with the event which begins a frame is the time range of that frame.
		game_frame_recoder.OnCPUStackTree(event_scope);

		// The query system has already filtered the thread, the time and the whole call chain of every
		// query, so every match which it visits is an occurrence which the query counts, and the
		// statistics of the match only have to be accumulated.
		//
		// Two matches of one query may be the same call, though: an event may appear in more than one
		// chain element, and the elements of a chain do not have to be the nearest descendants, so the
		// same event can be reported by several elements with overlapping time ranges. The matches of one
		// query are collected first, and the ones whose time ranges overlap are collapsed into the longest
		// one, the shorter ones are the same call seen from another element. The matches of two queries
		// are never compared, so the same event is still counted independently by both of them.
		std::pmr::vector<MatchedOccurrence> occurrences(&resource);

		query_mapping.ForEachMatchedEvent(event_scope, [&](EventQueryMapping::EventMatch const& match) {
			EventID event_id = match.event_view.event_id;

			for (std::size_t index = 0; index < occurrences.size();)
			{
				auto const& exist = occurrences[index];
				if (
					exist.query_index == match.query_index
					&& exist.event_view.event_id == event_id
					&& exist.event_view.time_range.IsOverlap(match.event_view.time_range)
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

			occurrences.push_back(MatchedOccurrence{ match.event_view, match.query_index });
		});

		// The occurrences which are left are the calls which have to be counted, the exclude time of an
		// occurrence is the duration of its subtree without the time of its child events. The statistic of the
		// occurrence is found by the query and by the id of the event, which is what the statistic is keyed by,
		// and the occurrence is dropped when no statistic carries it.
		for (auto const& occurrence : occurrences)
		{
			auto statistic = FindEventStatistic(occurrence.query_index, occurrence.event_view.event_id);
			if (statistic == nullptr)
			{
				continue;
			}

			DurationSec exclude_time = DurationSec::zero();
			if (auto exclude_statistic = event_scope.GetExcludeTime(occurrence.event_view); exclude_statistic)
			{
				exclude_time = exclude_statistic->active_time;
			}

			RecordEventOccurrence(event_scope, occurrence.event_view, exclude_time, *statistic);
		}
	}

	EventStatisticParser::EventStatistic* EventStatisticParser::FindEventStatistic(std::size_t query_index, EventID id)
	{
		// The statistic is keyed by the query and by the name of the event, and it carries the ids of the events which
		// the name belongs to, so the stack tree finds the statistic of its event by the id.
		for (auto& statistic : event_statistics)
		{
			if (statistic.query_index != query_index)
			{
				continue;
			}

			if (std::find(statistic.event_ids.begin(), statistic.event_ids.end(), id) != statistic.event_ids.end())
			{
				return &statistic;
			}
		}

		return nullptr;
	}

	EventStatisticParser::EventStatistic& EventStatisticParser::AddEventStatistic(std::size_t query_index, std::size_t node_index, EventID id, EventQuery::ChainElement::Kind kind, std::u8string_view event_name)
	{
		// The statistic of a merged element is keyed by the element itself: every event which the element matches shares
		// one statistic, and the name of the statistic is the regular expression of the element, so the report tells
		// which element the timing belongs to.
		if (kind == EventQuery::ChainElement::Kind::QueryAndStatisticMerged)
		{
			for (auto& statistic : event_statistics)
			{
				if (statistic.query_index != query_index || !statistic.is_merged || statistic.merged_node_index != node_index)
				{
					continue;
				}

				if (std::find(statistic.event_ids.begin(), statistic.event_ids.end(), id) == statistic.event_ids.end())
				{
					statistic.event_ids.push_back(id);
				}
				return statistic;
			}

			auto& statistic = event_statistics.emplace_back(&resource);
			statistic.query_index = query_index;
			statistic.is_merged = true;
			statistic.merged_node_index = node_index;
			statistic.event_ids.push_back(id);

			// The name of a merged statistic is the regular expression of its element, it is written back with the
			// delimiters of the query grammar, so the report tells which element the timing belongs to.
			auto const& node = query_mapping.GetQueries()[query_index].nodes[node_index];
			if (node.matcher)
			{
				std::string_view pattern = node.matcher->pattern();
				statistic.event_name.push_back(u8'\u005e');
				statistic.event_name.append(
					reinterpret_cast<char8_t const*>(pattern.data()),
					static_cast<std::size_t>(pattern.size())
				);
				statistic.event_name.push_back(u8'\u005e');
			}

			return statistic;
		}

		// The statistic of a normal element is found by the name first: the events which carry the same name share one
		// statistic inside one query, and only the id is pushed into it, so the name is only copied when it is not
		// counted yet.
		for (auto& statistic : event_statistics)
		{
			if (statistic.query_index != query_index || statistic.is_merged || statistic.event_name != event_name)
			{
				continue;
			}

			if (std::find(statistic.event_ids.begin(), statistic.event_ids.end(), id) == statistic.event_ids.end())
			{
				statistic.event_ids.push_back(id);
			}
			return statistic;
		}

		auto& statistic = event_statistics.emplace_back(&resource);
		statistic.query_index = query_index;
		statistic.event_ids.push_back(id);
		statistic.event_name.assign(event_name.begin(), event_name.end());
		return statistic;
	}

	EventStatisticParser::EventStatistic& EventStatisticParser::AddEventStatistic(std::size_t query_index, std::size_t node_index, EventID id, EventQuery::ChainElement::Kind kind, std::wstring_view event_name)
	{
		// The name of an event spec is wide, it is encoded into the temporary storage first, so the name is compared and
		// copied in the same form as the name which is UTF-8 already.
		std::pmr::u8string encoded_event_name{ &resource };
		Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(event_name, std::back_inserter(encoded_event_name));
		return AddEventStatistic(query_index, node_index, id, kind, std::u8string_view{ encoded_event_name });
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
		statistic.occurrence_time_ranges.push_back(event_view.time_range);

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

		// The occurrence is kicked out right away when it is the shortest one of a full list, so the
		// occurrences which do not rank in the top N of the statistic are never kept.
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

			// The statistics which are merged by the count of the occurrences inside one game frame are built now, when
			// the frames are complete, and the game frame in which the event took the most time is found as well.
			BuildFrameStatistic(statistic);

			// The histogram of the duration of the single occurrence is built from the sorted durations as well.
			BuildDurationDistribution(statistic);

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
			}

			// Report the longest occurrence of the top list first, the shortest one is the last, the
			// occurrence which did not rank in the top N is not in the list at all.
			std::sort(statistic.top_occurrences.begin(), statistic.top_occurrences.end(), [](TopEventOccurrence const& left, TopEventOccurrence const& right) {
				return left.time_range.Size() > right.time_range.Size();
				});
		}
	}

	void EventStatisticParser::BuildFrameStatistic(EventStatistic& statistic)
	{
		std::size_t frame_count = game_frame_recoder.GetFrameCount();
		if (frame_count == 0 || statistic.occurrence_time_ranges.empty())
		{
			return;
		}

		// The count of the occurrences of the event and the time which they took inside every game frame.
		struct FrameRecord
		{
			std::size_t count = 0;
			DurationSec total_duration = DurationSec::zero();
		};

		std::pmr::vector<FrameRecord> frame_records(&resource);
		frame_records.resize(frame_count);

		for (auto& occurrence_time_range : statistic.occurrence_time_ranges)
		{
			// The frame of an occurrence is the one which carries its beginning; the recorder locates the first frame
			// whose end is greater than the point, and a point which is before the begin of that frame belongs to no frame.
			std::size_t frame_index = game_frame_recoder.LocateFirstFrameIndex(occurrence_time_range.Begin());
			if (frame_index >= frame_count)
			{
				continue;
			}

			if (occurrence_time_range.Begin() < game_frame_recoder.GetFrameTimeRange(frame_index).Begin())
			{
				continue;
			}

			frame_records[frame_index].count += 1;
			frame_records[frame_index].total_duration += occurrence_time_range.Size();
		}

		// The frames are merged by the count of the occurrences which they carry: every count keeps how many frames carry
		// it and the sum of the time which the event took inside those frames, which is averaged below.
		for (std::size_t frame_index = 0; frame_index < frame_records.size(); ++frame_index)
		{
			auto const& frame_record = frame_records[frame_index];
			if (frame_record.count == 0)
			{
				continue;
			}

			auto find = std::find_if(
				statistic.frame_count_statistics.begin(),
				statistic.frame_count_statistics.end(),
				[&](EventStatistic::FrameCountStatistic const& ite) { return ite.count == frame_record.count; }
			);

			if (find == statistic.frame_count_statistics.end())
			{
				auto& frame_count_statistic = statistic.frame_count_statistics.emplace_back();
				frame_count_statistic.count = frame_record.count;
				frame_count_statistic.frame_count = 1;
				frame_count_statistic.mean_duration = frame_record.total_duration;
				frame_count_statistic.longest_frame_time_range = game_frame_recoder.GetFrameTimeRange(frame_index);
				frame_count_statistic.longest_frame_duration = frame_record.total_duration;
			}
			else {
				find->frame_count += 1;
				find->mean_duration += frame_record.total_duration;

				// The longest frame is only kept inside the count which it carries, so every count tells the frame in which
				// the event took the most time when it occurred that count of times in one frame.
				if (frame_record.total_duration > find->longest_frame_duration)
				{
					find->longest_frame_duration = frame_record.total_duration;
					find->longest_frame_time_range = game_frame_recoder.GetFrameTimeRange(frame_index);
				}
			}
		}

		// The sum of the time of a count is averaged by the count of the frames which carry it, and the counts are
		// reported in the ascending order.
		for (auto& frame_count_statistic : statistic.frame_count_statistics)
		{
			if (frame_count_statistic.frame_count > 0)
			{
				frame_count_statistic.mean_duration = frame_count_statistic.mean_duration / static_cast<double>(frame_count_statistic.frame_count);
			}
		}

		std::sort(
			statistic.frame_count_statistics.begin(),
			statistic.frame_count_statistics.end(),
			[](EventStatistic::FrameCountStatistic const& left, EventStatistic::FrameCountStatistic const& right) {
				return left.count < right.count;
			}
		);
	}


	void EventStatisticParser::BuildDurationDistribution(EventStatistic& statistic)
	{
		// The histogram is only built for the events which occur more than top_count times, a histogram of a few
		// occurrences tells nothing.
		if (duration_bucket_count == 0 || statistic.count <= top_count || statistic.durations.empty())
		{
			return;
		}

		// The durations are sorted before this is called, so the first one is the shortest and the last one is the
		// longest, and the range between them is split into duration_bucket_count buckets of the same width.
		double min_duration = statistic.durations.front().count();
		double max_duration = statistic.durations.back().count();
		double bucket_width = (max_duration - min_duration) / static_cast<double>(duration_bucket_count);

		statistic.duration_distribution.resize(duration_bucket_count);
		for (std::size_t bucket_index = 0; bucket_index < duration_bucket_count; ++bucket_index)
		{
			auto& bucket = statistic.duration_distribution[bucket_index];
			bucket.duration_range = Potato::Misc::IndexSpan<DurationSec>{
				DurationSec{ min_duration + bucket_width * static_cast<double>(bucket_index) },
				DurationSec{ min_duration + bucket_width * static_cast<double>(bucket_index + 1) }
			};
			bucket.count = 0;
			bucket.percent = 0.0;
		}

		for (auto const& duration : statistic.durations)
		{
			// The bucket of a duration is found by its distance from the shortest one, and the longest duration, which
			// is on the end of the last bucket, is counted by the last bucket.
			std::size_t bucket_index = duration_bucket_count - 1;
			if (bucket_width > 0.0)
			{
				std::size_t index = static_cast<std::size_t>((duration.count() - min_duration) / bucket_width);
				if (index < duration_bucket_count)
				{
					bucket_index = index;
				}
			}

			auto& bucket = statistic.duration_distribution[bucket_index];
			bucket.count += 1;
			bucket.percent = static_cast<double>(bucket.count) / static_cast<double>(statistic.count) * 100.0;
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

		// The name of the event is the copy which the statistic carries, so the report is built from the statistic
		// alone. The name of a merged statistic is the regular expression of its element, which is reported with the
		// label of the regex, and the count of the merged events is reported with it.
		std::u8string_view event_name = statistic.event_name.empty()
			? std::u8string_view{ u8"<Unknow>" }
			: std::u8string_view{ statistic.event_name };

		// The statistic is only reported when it has at least one occurrence, so the count is never zero
		// here.
		double mean_duration = (statistic.total_duration / static_cast<double>(statistic.count)).count() * 1000.0;
		double mean_exclude_time = (statistic.total_exclude_time / static_cast<double>(statistic.count)).count() * 1000.0;

		if (statistic.is_merged)
		{
			OutputLog<L"            Regex:<{:}>, Merged Event(s):<{}>, Count:<{}>, Total D:<{:.3f}ms>, Mean D:<{:.3f}ms>, Median D:<{:.3f}ms>, Std D:<{:.3f}ms>, Total E:<{:.3f}ms>, Mean E:<{:.3f}ms>">(
				printer,
				event_name,
				statistic.event_ids.size(),
				statistic.count,
				statistic.total_duration.count() * 1000.0,
				mean_duration,
				statistic.median_duration.count() * 1000.0,
				statistic.duration_standard_deviation.count() * 1000.0,
				statistic.total_exclude_time.count() * 1000.0,
				mean_exclude_time
			);
		}
		else {
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
		}
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

		// The statistics which are merged by the count of the occurrences inside one game frame: every count keeps how
		// many game frames carry it, the mean of the time which the event took inside those frames, and the frame of that
		// count in which the event took the most time.
		if (!statistic.frame_count_statistics.empty())
		{
			OutputLog<L"                GameFrame Statistics:">(printer);

			// Every count of the occurrences inside one game frame keeps how many frames carry it, the mean of the time which
			// the event took inside those frames, and the frame of that count in which it took the most time.
			for (auto const& frame_count_statistic : statistic.frame_count_statistics)
			{
				OutputLog<L"                    <{} Time(s) In One Frame>: Frames <{}>, Mean Total D:<{:.3f}ms>, Longest TR:<[{}, {}]>, Longest D:<{:.3f}ms>">(
					printer,
					frame_count_statistic.count,
					frame_count_statistic.frame_count,
					frame_count_statistic.mean_duration.count() * 1000.0,
					DisplayTimePoint{ frame_count_statistic.longest_frame_time_range.Begin() },
					DisplayTimePoint{ frame_count_statistic.longest_frame_time_range.End() },
					frame_count_statistic.longest_frame_duration.count() * 1000.0
				);
			}
		}

		// The histogram of the duration of the single occurrence: the range of every bucket and the share of the
		// occurrences which fell into it.
		if (!statistic.duration_distribution.empty())
		{
			OutputLog<L"                Duration Distribution <{}> Bucket(s):">(printer, statistic.duration_distribution.size());

			for (auto const& bucket : statistic.duration_distribution)
			{
				OutputLog<L"                    <[{:.3f}ms, {:.3f}ms]>, Count:<{}>, Percent:<{:.2f}%>">(
					printer,
					bucket.duration_range.Begin().count() * 1000.0,
					bucket.duration_range.End().count() * 1000.0,
					bucket.count,
					bucket.percent
				);
			}
		}
	}

	bool EventStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventStatisticParser Output:">(printer);

		OutputLog<L"    (D: Duration of the single occurrence; E: Exclude Time, the duration without the time of the child events; ENCS: Exclude Time WithOut Context Switch Time; CS: Context Switch Time, the time which the thread spent out of the CPU in the time range of the occurrence; Mean D / Median D / Std D: the mean, the median and the standard deviation of the single duration; TName: the name of the thread which the occurrence happened on; TR: Time Range of the occurrence)">(
			printer
		);

		auto queries = query_mapping.GetQueries();
		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto const& query = queries[query_index];

			// The query is printed back from the regular expressions of its elements, with the same delimiters which the
			// grammar of the query accepts, so the printed query can be used as an input query again: '(^Re^)' for a
			// query only element, '[^Re^]' for an element which collects its timing, '%[^Re^]' for an element which
			// merges the timing of every event which it matches, and '%' for the element which matches any event.
			std::wstring header = std::format(L"    Query <{}>:", query_index);
			for (std::size_t node_index = 0; node_index < query.nodes.size(); ++node_index)
			{
				auto const& node = query.nodes[node_index];
				header += node_index == 0 ? L" " : L" -> ";

				switch (node.kind)
				{
				case EventQuery::ChainElement::Kind::Any:
					// The element matches any event, it carries no regular expression.
					header += L"%";
					break;
				case EventQuery::ChainElement::Kind::QueryOnly:
					header += L"(";
					break;
				case EventQuery::ChainElement::Kind::QueryAndStatistic:
					header += L"[";
					break;
				case EventQuery::ChainElement::Kind::QueryAndStatisticMerged:
					header += L"%[";
					break;
				}

				if (node.matcher)
				{
					// The regular expression is written back with the '^' which the grammar of the query surrounds it
					// with, and the element is closed with the delimiter which its kind opened.
					header += L"^";
					header += EncodePattern(*node.matcher);
					header += L"^";
				}

				switch (node.kind)
				{
				case EventQuery::ChainElement::Kind::Any:
					break;
				case EventQuery::ChainElement::Kind::QueryOnly:
					header += L")";
					break;
				default:
					header += L"]";
					break;
				}
			}

			if (query.thread_matcher)
			{
				header += L" & Thread=(^";
				header += EncodePattern(*query.thread_matcher);
				header += L"^)";
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
			std::size_t counted_event_count = 0;
			for (auto const& statistic : event_statistics)
			{
				if (statistic.query_index == query_index && statistic.count != 0)
				{
					++counted_event_count;
				}
			}

			if (counted_event_count == 0)
			{
				continue;
			}

			OutputLog<L"        <{}> Event(s):">(printer, counted_event_count);

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