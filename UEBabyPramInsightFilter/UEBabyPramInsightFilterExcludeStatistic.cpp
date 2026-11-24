module;

#include <cmath>
#include <re2/re2.h>

module UEBabyPramInsightFilterExcludeStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		std::u8string ToUtf8(std::wstring_view text)
		{
			std::u8string utf8;
			Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(text, std::back_inserter(utf8));
			return utf8;
		}

		std::wstring ToWide(std::u8string_view text)
		{
			std::wstring wide;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(text, std::back_inserter(wide));
			return wide;
		}

		// Every time of the row is reported in milliseconds, except the time range which stays in the m/s form of the
		// trace. At most three decimals are kept, and the trailing zeros are removed, so 5.715000 is written as 5.715.
		std::wstring FormatMilliseconds(DurationSec duration)
		{
			std::wstring text = std::format(L"{:.3f}", duration.count() * 1000.0);

			while (!text.empty() && text.back() == L'0')
			{
				text.pop_back();
			}

			if (!text.empty() && text.back() == L'.')
			{
				text.pop_back();
			}

			text += L" ms";
			return text;
		}
	}

	ExcludeStatisticParser::ExcludeStatisticParser(Config in_config)
		: resource(in_config.resource)
		, query_mapping(EventQueryMapping::Config{
			std::vector<EventQuery>{ std::move(in_config.query) },
			&resource
		})
		, events_manager(EventSpecManager::Config{ false, &resource })
		, event_statistics(&resource)
		, report_order(&resource)
		, top_count(in_config.top_count)
	{
	}

	void ExcludeStatisticParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		query_mapping.AddThread(thread_id, thread_system_id, thread_name);
	}

	void ExcludeStatisticParser::RecordEventName(EventID id, std::u8string_view event_name)
	{
		event_names.try_emplace(id.id, std::u8string{ event_name });
	}

	void ExcludeStatisticParser::RecordEventName(EventID id, std::wstring_view event_name)
	{
		event_names.try_emplace(id.id, ToUtf8(event_name));
	}

	void ExcludeStatisticParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view, std::size_t)
	{
		// The events manager dedups the event, so the query system is asked about it once and the dedup lookup of the
		// query system is skipped.
		if (!events_manager.AddEvent(id, event_name, {}, 0))
		{
			return;
		}

		auto result = query_mapping.AddEvent(id, event_name, {}, true);
		if (result.is_matched)
		{
			RecordEventName(id, event_name);
		}
	}

	void ExcludeStatisticParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger is reported once per scope instance, so it is deduplicated by the events
		// manager instead of by the query system, which is only asked about the event once.
		if (!events_manager.AddEvent(id, event_name, {}, 0))
		{
			return;
		}

		auto result = query_mapping.AddEvent(id, event_name, {}, true);
		if (result.is_matched)
		{
			RecordEventName(id, event_name);
		}
	}

	void ExcludeStatisticParser::RecordOccurrence(std::u8string_view event_name, Potato::Misc::IndexSpan<DurationSec> time_range, DurationSec exclude_time)
	{
		auto found = statistic_indices.find(event_name);
		if (found == statistic_indices.end())
		{
			EventStatistic statistic{ &resource };
			statistic.name.assign(event_name.begin(), event_name.end());

			std::size_t index = event_statistics.size();
			statistic_indices.emplace(statistic.name, index);
			event_statistics.push_back(std::move(statistic));
			found = statistic_indices.find(event_name);
		}

		EventStatistic& statistic = event_statistics[found->second];
		++statistic.count;
		statistic.total_exclude_time += exclude_time;
		statistic.exclude_times.push_back(exclude_time);

		if (statistic.count == 1 || exclude_time > statistic.max_exclude_time)
		{
			statistic.max_exclude_time = exclude_time;
			statistic.max_time_range = time_range;
			statistic.max_duration = time_range.Size();
		}
	}

	void ExcludeStatisticParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		// The query system has already filtered the chain, the time and the thread, so every match is an occurrence
		// which the query counts. Two matches of the same event whose time ranges overlap are the same call seen from
		// two chain elements, so only the longer one is counted, the same as the statistics of -es.
		std::pmr::vector<ThreadCPUEventView::EventView> occurrences(&resource);

		query_mapping.ForEachMatchedEvent(event_scope, [&](EventQueryMapping::EventMatch const& match) {
			ThreadCPUEventView::EventView event_view = match.event_view;

			for (std::size_t index = 0; index < occurrences.size();)
			{
				auto const& exist = occurrences[index];
				if (
					exist.event_id == event_view.event_id
					&& exist.time_range.IsOverlap(event_view.time_range)
					)
				{
					if (exist.time_range.Size() >= event_view.time_range.Size())
					{
						return;
					}

					occurrences.erase(occurrences.begin() + index);
					continue;
				}

				++index;
			}

			occurrences.push_back(event_view);
		});

		for (auto const& occurrence : occurrences)
		{
			auto const name = event_names.find(occurrence.event_id.id);
			if (name == event_names.end())
			{
				continue;
			}

			DurationSec exclude_time = DurationSec::zero();
			if (auto exclude_statistic = event_scope.GetExcludeTime(occurrence); exclude_statistic)
			{
				exclude_time = exclude_statistic->active_time;
			}

			RecordOccurrence(name->second, occurrence.time_range, exclude_time);
		}
	}

	bool ExcludeStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the names, the metadata carries the spec of the CPU scope logger, and the CPU event
		// carries the stack tree which carries the timing of every occurrence.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT;
	}

	bool ExcludeStatisticParser::IsThreadRequired(ThreadID thread_id) const
	{
		return query_mapping.IsThreadMatched(thread_id);
	}

	bool ExcludeStatisticParser::IsThreadRequired(ThreadSystemID thread_system_id) const
	{
		auto thread_info = GetThreadInfo(thread_system_id);
		if (!thread_info)
		{
			return false;
		}

		return query_mapping.IsThreadMatched(thread_info->thread_id);
	}

	void ExcludeStatisticParser::AllAnalyzeDone()
	{
		for (auto& statistic : event_statistics)
		{
			if (statistic.count == 0)
			{
				continue;
			}

			statistic.mean_exclude_time = DurationSec{
				statistic.total_exclude_time.count() / static_cast<double>(statistic.count)
			};

			std::sort(statistic.exclude_times.begin(), statistic.exclude_times.end());
			std::size_t middle = statistic.exclude_times.size() / 2;
			statistic.median_exclude_time = statistic.exclude_times.size() % 2 == 0
				? DurationSec{
					(statistic.exclude_times[middle - 1].count() + statistic.exclude_times[middle].count()) / 2.0
				}
				: statistic.exclude_times[middle];

			long double squared_deviation = 0.0L;
			for (DurationSec exclude_time : statistic.exclude_times)
			{
				long double difference = static_cast<long double>(exclude_time.count() - statistic.mean_exclude_time.count());
				squared_deviation += difference * difference;
			}

			statistic.standard_deviation = DurationSec{
				std::sqrt(static_cast<double>(squared_deviation / static_cast<long double>(statistic.count)))
			};
		}

		report_order.resize(event_statistics.size());
		for (std::size_t index = 0; index < event_statistics.size(); ++index)
		{
			report_order[index] = index;
		}

		std::sort(report_order.begin(), report_order.end(), [this](std::size_t left, std::size_t right) {
			auto const& left_statistic = event_statistics[left];
			auto const& right_statistic = event_statistics[right];

			if (left_statistic.max_exclude_time != right_statistic.max_exclude_time)
			{
				return left_statistic.max_exclude_time > right_statistic.max_exclude_time;
			}

			return left_statistic.name < right_statistic.name;
		});

		Parser::AllAnalyzeDone();
	}

	bool ExcludeStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		std::size_t reported_count = top_count == 0
			? report_order.size()
			: std::min(top_count, report_order.size());

		PrintReportLine(printer, L"ExcludeStatisticParser Output:");
		PrintReportLine(printer, std::format(
			L"    Event <{}>, Top <{}>",
			event_statistics.size(),
			reported_count
		));

		if (reported_count == 0)
		{
			PrintReportLine(printer, L"    No event is selected by the query, or the trace carries no CPU stack tree.");
			return true;
		}

		for (std::size_t rank = 0; rank < reported_count; ++rank)
		{
			EventStatistic const& statistic = event_statistics[report_order[rank]];

			PrintReportLine(printer, std::format(
				L"    [{}] {}",
				rank + 1,
				ToWide(statistic.name)
			));

			PrintReportLine(printer, std::format(
				L"        Max Exclude:<{}>, Range:<{}, {}>, Duration:<{}>",
				FormatMilliseconds(statistic.max_exclude_time),
				FormatTimePoint(statistic.max_time_range.Begin()),
				FormatTimePoint(statistic.max_time_range.End()),
				FormatMilliseconds(statistic.max_duration)
			));

			PrintReportLine(printer, std::format(
				L"        Count:<{}>, Mean Exclude:<{}>, Median Exclude:<{}>, StdDev Exclude:<{}>",
				statistic.count,
				FormatMilliseconds(statistic.mean_exclude_time),
				FormatMilliseconds(statistic.median_exclude_time),
				FormatMilliseconds(statistic.standard_deviation)
			));
		}

		return true;
	}
}
