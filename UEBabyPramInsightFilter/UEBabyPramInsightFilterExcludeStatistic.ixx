module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterExcludeStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Report the events which the query selects, ordered by the highest Exclude Time of their occurrences. The event
	// name is the key: every occurrence of the same name is counted into one statistic, and the occurrence which has
	// the highest Exclude Time of the name is the one which the row reports. The row also carries the count, the mean,
	// the median and the standard deviation of the Exclude Time of every occurrence of the name.
	struct ExcludeStatisticParser : public Parser
	{
		struct Config
		{
			// The query of the analysis, it is built by ParseEventQuery and checked by the command line, so it only
			// carries the conditions which the parser reads.
			EventQuery query;
			// How many event names the report lists, 0 for no limit, the default is 50.
			std::size_t top_count = 50;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The statistic of one event name. The occurrences are counted by name, so two event ids which carry the same
		// name share the statistic.
		struct EventStatistic
		{
			explicit EventStatistic(std::pmr::memory_resource* resource)
				: name(resource)
				, exclude_times(resource) {}

			std::pmr::u8string name;
			std::size_t count = 0;
			DurationSec total_exclude_time = DurationSec::zero();
			// The Exclude Time of every occurrence, it is sorted in AllAnalyzeDone() for the median and the standard
			// deviation.
			std::pmr::vector<DurationSec> exclude_times;
			// The occurrence which carries the highest Exclude Time of the name: its Exclude Time, its time range and
			// its duration.
			DurationSec max_exclude_time = DurationSec::zero();
			Potato::Misc::IndexSpan<DurationSec> max_time_range;
			DurationSec max_duration = DurationSec::zero();
			// The statistics of the Exclude Time of every occurrence of the name.
			DurationSec mean_exclude_time = DurationSec::zero();
			DurationSec median_exclude_time = DurationSec::zero();
			DurationSec standard_deviation = DurationSec::zero();
		};

		ExcludeStatisticParser(Config config = {});

		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		virtual bool IsThreadRequired(ThreadSystemID thread_system_id) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The conditions which the query may carry, which are the same as the ones of -es.
		static constexpr QueryConditionFlag AcceptedQueryCondition()
		{
			return QueryConditionFlag::Time | QueryConditionFlag::Thread;
		}

		std::span<EventStatistic const> GetEventStatistics() const { return event_statistics; }

	protected:

		// Store the name of an event id, the name is what the occurrences of every id are counted by.
		void RecordEventName(EventID id, std::u8string_view event_name);
		void RecordEventName(EventID id, std::wstring_view event_name);

		// Add one occurrence to the statistic of its event name, and replace the highest occurrence of the name when
		// this one carries a higher Exclude Time.
		void RecordOccurrence(std::u8string_view event_name, Potato::Misc::IndexSpan<DurationSec> time_range, DurationSec exclude_time);

		std::pmr::unsynchronized_pool_resource resource;
		// The query system which resolves the chain, the time and the thread conditions and tells which occurrences the
		// query counts.
		EventQueryMapping query_mapping;
		std::pmr::vector<EventStatistic> event_statistics;
		// The statistic of every event name, the name is the key.
		std::map<std::u8string, std::size_t, std::less<>> statistic_indices;
		// The name of every event id, so an occurrence finds the name which it is counted by.
		std::unordered_map<std::size_t, std::u8string> event_names;
		// The event names ordered by the highest Exclude Time, the order is built in AllAnalyzeDone().
		std::pmr::vector<std::size_t> report_order;
		std::size_t top_count = 50;
	};
}
