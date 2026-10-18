module;
#include <cassert>
module UEBabyPramInsightFilterEventStatisticParser;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	EventStatisticParser::EventStatisticParser(Config in_config)
		: resource(in_config.resource)
		, events_manager(EventSpecManager::Config{ 10000, 1000, false, &resource })
		, matcher(std::move(in_config.matcher))
		, matcher_error(DescribeMatcherError(matcher))
		, matched_event_index(&resource)
		, event_statistics(&resource)
		, top_records(&resource)
		, thread_names(&resource)
		, top_count(in_config.top_count)
	{
	}

	void EventStatisticParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		if (!thread_names.contains(thread_id.id))
		{
			auto& name = thread_names[thread_id.id];
			Potato::Encode::UnicodeEncoder<char, wchar_t>::EncodeTo(
				thread_name,
				std::back_inserter(name)
			);
		}
	}

	// Every event spec gets its own statistic here, the statistics which share the same event name
	// are merged in AllAnalyzeDone(), so this is a constant time operation per event spec.
	std::size_t EventStatisticParser::AddEventStatistic(EventID id, std::wstring_view event_name)
	{
		std::size_t index = event_statistics.size();
		event_statistics.emplace_back(&resource).event_name.assign(event_name);
		matched_event_index.emplace(id, index);
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
			if (matcher_error.empty() && !event_name.empty() && IsEventNameMatched(*matcher, event_name, encoded_event_name))
			{
				AddEventStatistic(id, event_name);
			}
		}
	}

	void EventStatisticParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, its name is the only
		// information of the spec, but it is carried by the stack tree as well, so it is handled
		// the same way as the event of the event spec.
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			if (matcher_error.empty() && !event_name.empty())
			{
				std::wstring name_storage;
				Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(
					event_name,
					std::back_inserter(name_storage)
				);

				if (IsEventNameMatched(*matcher, name_storage, encoded_event_name))
				{
					AddEventStatistic(id, name_storage);
				}
			}
		}
	}

	bool EventStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the name of every event, the metadata carries the spec of the
		// event of the CPU scope logger, and the CPU event carries the stack tree which carries
		// the timing of every event. The context switch event is not required, the exclude time
		// does not remove the time of the context switch.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT;
	}

	auto EventStatisticParser::CopyEventName(EventID id) -> std::pmr::wstring
	{
		auto spec = events_manager.GetEventSpec(id);
		if (spec.name_view.empty())
		{
			return std::pmr::wstring{ std::wstring_view{ L"<Unknow>" }, &resource };
		}
		return std::pmr::wstring{ spec.name_view, &resource };
	}

	auto EventStatisticParser::GetThreadName(ThreadID thread_id) -> std::wstring_view
	{
		if (auto find = thread_names.find(thread_id.id); find != thread_names.end())
		{
			return find->second;
		}
		return {};
	}

	void EventStatisticParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (matcher_error.empty() && !matched_event_index.empty())
		{
			// Every event of the stack tree is visited once, in the order of the beginning of the event.
			std::size_t index_offset = 0;
			while (index_offset < event_scope.view.size())
			{
				auto current_event = event_scope.FindNextEvent({}, index_offset);

				if (!current_event)
				{
					break;
				}

				if (auto find = matched_event_index.find(current_event.event_id); find != matched_event_index.end())
				{
					RecordSample(event_scope, current_event, find->second);
				}

				index_offset = current_event.index_range.Begin() + 1;
			}
		}
	}

	void EventStatisticParser::RecordSample(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, std::size_t statistic_index)
	{
		auto exclude_time = event_scope.GetExcludeTime(event_view);
		assert(exclude_time.has_value());
		if (!exclude_time)
		{
			return;
		}

		DurationSec duration = event_view.time_range.Size();

		auto& statistic = event_statistics[statistic_index];
		statistic.samples.push_back(EventSample{ duration, exclude_time->active_time });

		if (top_count == 0)
		{
			return;
		}

		// Keep the list of the longest occurrences, the call chain is only copied for a new entry.
		if (top_records.size() >= top_count)
		{
			auto shortest = std::min_element(top_records.begin(), top_records.end(), [](TopEventRecord const& left, TopEventRecord const& right) {
				return left.duration < right.duration;
				});
			if (shortest != top_records.end() && duration <= shortest->duration)
			{
				return;
			}
		}

		TopEventRecord& record = top_records.emplace_back(&resource);
		record.event_name.assign(statistic.event_name);
		record.thread_id = event_scope.thread_id;
		record.thread_name.assign(GetThreadName(event_scope.thread_id));
		record.duration = duration;
		record.exclude_time = exclude_time->active_time;

		// Collect the enclosing events, from the innermost to the outermost, the parent of an event
		// is the closest event which is one level above it and begins before it.
		std::size_t target_depth = event_view.depth;
		for (std::size_t cursor = event_view.index_range.Begin(); cursor > 0 && target_depth > 0;)
		{
			--cursor;
			auto const& ref = event_scope.view[cursor];
			if (ref.event_id && ref.depth + 1 == target_depth)
			{
				record.call_chain.push_back(CopyEventName(ref.event_id));
				target_depth = ref.depth;
			}
		}

		std::reverse(record.call_chain.begin(), record.call_chain.end());
		record.call_chain.push_back(std::pmr::wstring{ std::wstring_view{ record.event_name }, &resource });

		if (top_records.size() > top_count)
		{
			auto shortest = std::min_element(top_records.begin(), top_records.end(), [](TopEventRecord const& left, TopEventRecord const& right) {
				return left.duration < right.duration;
				});
			if (shortest != top_records.end())
			{
				top_records.erase(shortest);
			}
		}
	}

	void EventStatisticParser::MergeEventStatistics()
	{
		// The same event name may be defined in several places, which gives several event specs,
		// merge their samples so that every event name is reported once.
		std::sort(event_statistics.begin(), event_statistics.end(), [](EventStatistic const& left, EventStatistic const& right) {
			return left.event_name < right.event_name;
			});

		std::size_t output_index = 0;
		for (std::size_t index = 1; index < event_statistics.size(); ++index)
		{
			if (event_statistics[index].event_name == event_statistics[output_index].event_name)
			{
				auto& target = event_statistics[output_index];
				auto& source = event_statistics[index];
				if (target.samples.size() < source.samples.size())
				{
					// Append the smaller list to the bigger one, to move the memory of the samples
					// as seldom as possible.
					std::swap(target.samples, source.samples);
				}
				target.samples.insert(
					target.samples.end(),
					std::make_move_iterator(source.samples.begin()),
					std::make_move_iterator(source.samples.end())
				);
			}
			else {
				++output_index;
				if (output_index != index)
				{
					event_statistics[output_index] = std::move(event_statistics[index]);
				}
			}
		}

		event_statistics.erase(event_statistics.begin() + output_index + 1, event_statistics.end());
	}

	void EventStatisticParser::BuildEventStatistic(EventStatistic& statistic)
	{
		// Sort the single occurrences by their own duration in ascending order.
		std::sort(statistic.samples.begin(), statistic.samples.end(), [](EventSample const& left, EventSample const& right) {
			return left.duration < right.duration;
			});

		statistic.count = statistic.samples.size();
		if (statistic.count == 0)
		{
			return;
		}

		DurationSec total_duration = DurationSec::zero();
		DurationSec total_exclude_time = DurationSec::zero();
		for (auto const& sample : statistic.samples)
		{
			total_duration += sample.duration;
			total_exclude_time += sample.exclude_time;
		}

		statistic.mean_duration = total_duration / static_cast<double>(statistic.count);
		statistic.mean_exclude_time = total_exclude_time / static_cast<double>(statistic.count);

		// The median of the sorted durations, the middle sample for an odd count, and the mean of
		// the two middle samples for an even count.
		std::size_t middle_index = statistic.count / 2;
		if ((statistic.count % 2) != 0)
		{
			statistic.median_duration = statistic.samples[middle_index].duration;
		}
		else {
			statistic.median_duration = (statistic.samples[middle_index - 1].duration + statistic.samples[middle_index].duration) / 2.0;
		}

		// The standard deviation of the durations, the square root of the sum of the square of the
		// distance to the mean divided by the count, the two pass form keeps the precision.
		double square_distance = 0.0;
		for (auto const& sample : statistic.samples)
		{
			double distance = (sample.duration - statistic.mean_duration).count();
			square_distance += distance * distance;
		}
		statistic.duration_standard_deviation = DurationSec{ std::sqrt(square_distance / static_cast<double>(statistic.count)) };
	}

	void EventStatisticParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// The index of the matched event is only meaningful before the statistics are reordered.
		matched_event_index.clear();

		if (!matcher_error.empty())
		{
			event_statistics.clear();
			top_records.clear();
			return;
		}

		MergeEventStatistics();

		for (auto& statistic : event_statistics)
		{
			BuildEventStatistic(statistic);
		}

		// Report the shortest event first, so the events which cost the most time are at the end.
		std::sort(event_statistics.begin(), event_statistics.end(), [](EventStatistic const& left, EventStatistic const& right) {
			return left.mean_duration < right.mean_duration;
			});

		// Report the longest occurrence first.
		std::sort(top_records.begin(), top_records.end(), [](TopEventRecord const& left, TopEventRecord const& right) {
			return left.duration > right.duration;
			});
	}

	bool EventStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventStatisticParser Output:">(printer);

		if (!matcher_error.empty())
		{
			OutputLog<L"    Unsupport Event Matcher : <{}>">(printer, matcher_error);
			return false;
		}

		std::size_t total_count = 0;
		for (auto const& statistic : event_statistics)
		{
			total_count += statistic.count;
		}

		OutputLog<L"    Event Regex <{}>, Matched Event <{}>, Occurrence <{}>">(
			printer,
			std::string_view{ matcher->pattern() },
			event_statistics.size(),
			total_count
		);

		OutputLog<L"    (D: Duration of the single occurrence; E: Exclude Time, the duration without the time of the child events; Median D: the median of the single duration; Std D: the standard deviation of the single duration)">(
			printer
		);

		for (auto const& statistic : event_statistics)
		{
			OutputLog<L"    Name:<{:}>, Count:<{}>, Mean D:<{:.3f}ms>, Mean E:<{:.3f}ms>, Median D:<{:.3f}ms>, Std D:<{:.3f}ms>">(
				printer,
				std::wstring_view{ statistic.event_name },
				statistic.count,
				statistic.mean_duration.count() * 1000.0,
				statistic.mean_exclude_time.count() * 1000.0,
				statistic.median_duration.count() * 1000.0,
				statistic.duration_standard_deviation.count() * 1000.0
			);
		}

		OutputLog<L"{}Top <{}> Longest Occurrence:">(
			printer,
			printer.EndLineString(),
			top_count
		);

		std::size_t index = 0;
		for (auto const& record : top_records)
		{
			OutputLog<L"    <{:<4}> Name:<{:}>, Thread:<{:} No.{:}>, D:<{:.3f}ms>, E:<{:.3f}ms>">(
				printer,
				index,
				std::wstring_view{ record.event_name },
				std::wstring_view{ record.thread_name },
				record.thread_id.id,
				record.duration.count() * 1000.0,
				record.exclude_time.count() * 1000.0
			);

			std::wstring call_chain;
			for (auto const& frame : record.call_chain)
			{
				if (!call_chain.empty())
				{
					call_chain += L" -> ";
				}
				call_chain += frame;
			}

			OutputLog<L"        Chain:<{:}>, Chain Depth:<{}>">(
				printer,
				std::wstring_view{ call_chain },
				record.call_chain.size()
			);

			++index;
		}

		return true;
	}
}
