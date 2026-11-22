module;

#include <re2/re2.h>

module UEBabyPramInsightFilterContextSwitchStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// Sort the time ranges by the begin and merge the ranges which overlap or touch each other, so the
		// duration of the time window is the sum of the merged ranges and an event which crosses two adjacent
		// ranges is not counted twice.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> NormalizeTimeRanges(
			std::span<Potato::Misc::IndexSpan<DurationSec> const> ranges
		)
		{
			std::vector<Potato::Misc::IndexSpan<DurationSec>> sorted;
			sorted.reserve(ranges.size());
			for (auto const& range : ranges)
			{
				if (range.End() > range.Begin())
				{
					sorted.push_back(range);
				}
			}

			std::sort(sorted.begin(), sorted.end(), [](auto const& left, auto const& right) {
				return left.Begin() < right.Begin();
			});

			std::vector<Potato::Misc::IndexSpan<DurationSec>> merged;
			merged.reserve(sorted.size());
			for (auto const& range : sorted)
			{
				if (!merged.empty() && range.Begin() <= merged.back().End())
				{
					if (range.End() > merged.back().End())
					{
						merged.back() = Potato::Misc::IndexSpan<DurationSec>{ merged.back().Begin(), range.End() };
					}
				}
				else {
					merged.push_back(range);
				}
			}
			return merged;
		}
	}

	ContextSwitchStatisticParser::ContextSwitchStatisticParser(Config in_config)
		: resource(in_config.resource)
		, query(std::move(in_config.query))
		, query_text(std::move(in_config.query_text))
		, thread_filter(&resource)
		, thread_statistics(&resource)
	{
		has_time_condition = query.time_condition != EventQuery::TimeConditionKind::None;
		if (has_time_condition)
		{
			time_ranges = NormalizeTimeRanges(query.time_ranges);
		}
	}

	QueryConditionFlag ContextSwitchStatisticParser::AcceptedQueryCondition()
	{
		return QueryConditionFlag::Time | QueryConditionFlag::Thread;
	}

	std::u8string ContextSwitchStatisticParser::CheckQuery(EventQuery const& query)
	{
		std::u8string error = CheckQueryCondition(query, AcceptedQueryCondition());
		if (!error.empty())
		{
			return error;
		}

		for (auto const& element : query.chain)
		{
			if (element.kind != EventQuery::ChainElement::Kind::Any)
			{
				return u8"The chain of the context switch statistic query only accepts the '%' element: the chain is ignored by this parser, so a regular expression or a statistic element is not accepted";
			}
		}
		return {};
	}

	bool ContextSwitchStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The ContextSwitch events are the only events which this parser reads. The thread info is reported by
		// the analysis engine without a route, so no other flag is required.
		return flag == ParserRequireFlag::CONTEXT_SWITCH;
	}

	void ContextSwitchStatisticParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The whole range of the events is the fallback time window of a query which carries no time condition.
		if (!has_context_switch_event)
		{
			trace_range = duration;
			has_context_switch_event = true;
		}
		else {
			trace_range = Potato::Misc::IndexSpan<DurationSec>{
				std::min(trace_range.Begin(), duration.Begin()),
				std::max(trace_range.End(), duration.End())
			};
		}

		// The cores are collected before the thread filter, so the report tells which cores the system has
		// even when a core only carries events of a thread which is not selected.
		if (std::find(observed_cores.begin(), observed_cores.end(), core_name) == observed_cores.end())
		{
			observed_cores.push_back(core_name);
		}

		// The time which the event spends inside the time window. A query which carries no time condition uses
		// the whole event; an event which only overlaps a part of a slice is cut to it.
		DurationSec active_time = duration.Size();
		if (has_time_condition)
		{
			active_time = DurationSec::zero();
			for (auto const& range : time_ranges)
			{
				auto begin = std::max(duration.Begin(), range.Begin());
				auto end = std::min(duration.End(), range.End());
				if (end > begin)
				{
					active_time += end - begin;
				}
			}
		}

		// An event which is entirely outside the time window contributes neither time nor count.
		if (active_time <= DurationSec::zero())
		{
			return;
		}

		// The thread only counts when it is discovered and its name matches the Thread condition of the query.
		if (!IsThreadStatisticRequired(thread_id))
		{
			return;
		}

		auto result = thread_statistics.try_emplace(thread_id, thread_id, &resource);
		auto& thread = result.first->second;

		auto core = std::find_if(thread.cores.begin(), thread.cores.end(), [core_name](CoreStatistic const& statistic) {
			return statistic.core == core_name;
		});
		if (core == thread.cores.end())
		{
			thread.cores.push_back(CoreStatistic{ core_name, active_time, 1 });
		}
		else {
			core->active_time += active_time;
			core->count += 1;
		}

		thread.active_time += active_time;
		thread.count += 1;
	}

	bool ContextSwitchStatisticParser::IsThreadStatisticRequired(ThreadSystemID thread_id)
	{
		auto cached = thread_filter.find(thread_id);
		if (cached != thread_filter.end())
		{
			return cached->second;
		}

		auto info = GetThreadInfo(thread_id);
		if (!info || info->thread_name.empty())
		{
			// The thread is not discovered yet: it is not counted by this event, and the next event checks it
			// again, so a thread which is discovered later is counted from that point on.
			return false;
		}

		bool required = !query.thread_matcher
			|| re2::RE2::FullMatch(info->thread_name, *query.thread_matcher);
		thread_filter.emplace(thread_id, required);
		return required;
	}

	void ContextSwitchStatisticParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		std::sort(observed_cores.begin(), observed_cores.end());

		// The cores of a thread are reported in the order of their number, which is what the linear search
		// while the events are accumulated does not keep.
		for (auto& entry : thread_statistics)
		{
			auto& cores = entry.second.cores;
			std::sort(cores.begin(), cores.end(), [](CoreStatistic const& left, CoreStatistic const& right) {
				return left.core < right.core;
			});
		}
	}

	bool ContextSwitchStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"ContextSwitchStatisticParser Output:">(printer);
		OutputLog<L"    (Active clipped to Window; Share = Active/Window; Count = events; resolved threads matching Thread; threads sorted by Active)">(
			printer
		);
		OutputLog<L"    Query: <{:}>">(printer, std::u8string_view{ query_text });

		if (query.thread_matcher)
		{
			OutputLog<L"    Thread Regex: <{}>">(printer, std::string_view{ query.thread_matcher->pattern() });
		}
		else {
			OutputLog<L"    Thread Regex: <empty, every thread is counted>">(printer);
		}

		if (!has_context_switch_event)
		{
			OutputLog<L"    No Context Switch event is recorded in the trace.">(printer);
			return true;
		}

		// The time window is the merged slices of the query clipped to the range of the events. A query
		// without a time condition uses the whole range of the events.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> window;
		if (has_time_condition)
		{
			for (auto const& range : time_ranges)
			{
				auto begin = std::max(range.Begin(), trace_range.Begin());
				auto end = std::min(range.End(), trace_range.End());
				if (end > begin)
				{
					window.push_back(Potato::Misc::IndexSpan<DurationSec>{ begin, end });
				}
			}
			window = NormalizeTimeRanges(window);
		}
		else if (trace_range.End() > trace_range.Begin())
		{
			window.push_back(trace_range);
		}

		DurationSec window_duration = DurationSec::zero();
		for (auto const& range : window)
		{
			window_duration += range.Size();
		}

		if (window_duration <= DurationSec::zero())
		{
			OutputLog<L"    No time slice intersects the recorded Context Switch events.">(printer);
			return true;
		}

		OutputLog<L"    Time Window Total: <{}>">(printer, DisplayTimePoint{ window_duration });
		for (auto const& range : window)
		{
			OutputLog<L"        [{}, {}]">(
				printer,
				DisplayTimePoint{ range.Begin() },
				DisplayTimePoint{ range.End() }
			);
		}

		// The overall share of every core: the sum of the active time of all counted threads on that core,
		// divided by the whole time window. The cores which the events carry are listed even when no counted
		// thread was active on them, so the cores of the system are still visible.
		struct CoreTotal
		{
			std::size_t core = 0;
			DurationSec active_time = DurationSec::zero();
			std::size_t count = 0;
		};

		std::vector<CoreTotal> core_totals;
		core_totals.reserve(observed_cores.size());
		for (auto core : observed_cores)
		{
			core_totals.push_back(CoreTotal{ core, DurationSec::zero(), 0 });
		}

		for (auto const& entry : thread_statistics)
		{
			for (auto const& core : entry.second.cores)
			{
				auto total = std::find_if(core_totals.begin(), core_totals.end(), [&core](CoreTotal const& item) {
					return item.core == core.core;
				});
				if (total != core_totals.end())
				{
					total->active_time += core.active_time;
					total->count += core.count;
				}
			}
		}

		OutputLog<L"    Core Share:">(printer);
		for (auto const& total : core_totals)
		{
			double share = total.active_time.count() / window_duration.count() * 100.0;
			OutputLog<L"        Core <{}>: Active:<{}>, Share:<{:.2f}%>, Count:<{}>">(
				printer,
				total.core,
				DisplayTimePoint{ total.active_time },
				share,
				total.count
			);
		}

		struct ThreadReport
		{
			ThreadSystemID system_id;
			std::string_view name;
			ThreadStatistic const* statistic;
		};

		std::pmr::vector<ThreadReport> reports{ &resource };
		for (auto const& entry : thread_statistics)
		{
			auto info = GetThreadInfo(entry.first);
			std::string_view name = info && !info->thread_name.empty()
				? info->thread_name
				: std::string_view{ "Unknown" };

			reports.push_back(ThreadReport{ entry.first, name, &entry.second });
		}

		std::sort(reports.begin(), reports.end(), [](ThreadReport const& left, ThreadReport const& right) {
			if (left.statistic->active_time != right.statistic->active_time)
			{
				return left.statistic->active_time > right.statistic->active_time;
			}
			if (auto order = left.name <=> right.name; order != 0)
			{
				return order < 0;
			}
			return left.system_id < right.system_id;
		});

		if (reports.empty())
		{
			OutputLog<L"    No thread is selected by the query, or none of the selected threads carries a Context Switch event.">(printer);
			return true;
		}

		for (auto const& report : reports)
		{
			auto const& statistic = *report.statistic;
			double thread_share = statistic.active_time.count() / window_duration.count() * 100.0;

			OutputLog<L"    Thread <{}>, Active:<{}>, Share:<{:.2f}%>, Count:<{}>">(
				printer,
				report.name,
				DisplayTimePoint{ statistic.active_time },
				thread_share,
				statistic.count
			);

			for (auto const& core : statistic.cores)
			{
				double core_share = core.active_time.count() / window_duration.count() * 100.0;

				OutputLog<L"        Core <{}>: Active:<{}>, Share:<{:.2f}%>, Count:<{}>">(
					printer,
					core.core,
					DisplayTimePoint{ core.active_time },
					core_share,
					core.count
				);
			}
		}

		return true;
	}
}
