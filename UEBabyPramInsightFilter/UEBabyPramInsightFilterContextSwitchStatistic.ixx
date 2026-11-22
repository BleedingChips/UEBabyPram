module;

export module UEBabyPramInsightFilterContextSwitchStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Collect the ContextSwitch events of the trace and report the active time of every thread on every
	// core.
	//
	// The query only selects a time window and the threads: the call chain is ignored and it only accepts
	// the '%' element, the time condition (Time or ITime) marks the window, and the Thread condition
	// selects the threads whose name is matched. A query which carries no time condition uses the whole
	// range of the ContextSwitch events, and a query which carries no thread condition counts every thread.
	//
	// Every ContextSwitch event carries the time range in which the thread was active on a core, so the
	// active time of a core is the time of the events of that core. An event which crosses the boundary of
	// the time window is clipped to the window, and an event which is entirely outside it is dropped. The
	// count of a core is the count of the events which contribute to the window, an event which is clipped
	// is counted once.
	//
	// The share of a core is its active time divided by the whole duration of the time window, so the
	// shares of the cores of one thread are the share of the time which the thread spent on every core.
	// The percentages of the cores of one thread therefore add up to the share of the thread, which is at
	// most 100% when the ContextSwitch events of the thread do not overlap.
	struct ContextSwitchStatisticParser : public Parser
	{
		struct Config
		{
			// The query of the analysis, it is built by ParseEventQuery and checked by CheckQuery, the
			// command line reports an unacceptable query before the parser is created.
			EventQuery query;
			// The text of the query, it is reported with the report.
			std::u8string query_text;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The statistic of one core of one thread.
		struct CoreStatistic
		{
			std::size_t core = 0;
			DurationSec active_time = DurationSec::zero();
			// How many ContextSwitch events of the core contribute to the time window.
			std::size_t count = 0;
		};

		// The statistic of one thread, its cores are ordered by the core number when the analysis is done.
		struct ThreadStatistic
		{
			ThreadStatistic(ThreadSystemID system_id, std::pmr::memory_resource* resource)
				: system_id(system_id), cores(resource) {}

			ThreadSystemID system_id;
			std::pmr::vector<CoreStatistic> cores;
			// The active time and the count of the whole thread, which are the sums of its cores.
			DurationSec active_time = DurationSec::zero();
			std::size_t count = 0;
		};

		ContextSwitchStatisticParser(Config config = {});

		// Accumulate the ContextSwitch event of the thread on the core, the event is clipped to the time
		// window of the query before its time is added.
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) override;
		// Only the ContextSwitch events are read. The CPU event, the event spec, the meta data and the memory
		// channel are not required, and the thread info is reported by the analysis engine without a route.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The conditions which the query of this parser accepts: it only reads the time window and the
		// threads.
		static QueryConditionFlag AcceptedQueryCondition();
		// The error of a query which this parser does not accept, empty when the query is accepted. The
		// chain only accepts the '%' element and is ignored, and the conditions are Time, ITime and Thread.
		// The message is human readable UTF-8.
		static std::u8string CheckQuery(EventQuery const& query);

		// The stored statistics, ordered by the system thread id.
		std::pmr::unordered_map<ThreadSystemID, ThreadStatistic> const& GetThreadStatistics() const { return thread_statistics; }
		// The cores which any ContextSwitch event of the trace carries, ordered by the core number. They are
		// collected before the thread filter, so the Core Share report still lists every core of the system
		// even when no counted thread was active on it.
		std::vector<std::size_t> const& GetObservedCores() const { return observed_cores; }
		// The whole range of the ContextSwitch events of the trace.
		Potato::Misc::IndexSpan<DurationSec> const& GetTraceRange() const { return trace_range; }
		// Whether the trace carries a ContextSwitch event.
		bool HasContextSwitchEvent() const { return has_context_switch_event; }

	protected:

		// Whether the event of the thread has to be counted: the thread has to be discovered (GetThreadInfo
		// returns a name) and its name has to match the Thread condition of the query. The result is cached
		// for a thread which is discovered, and a thread which is not discovered yet is checked again by the
		// next event, so a thread which is discovered later is counted from that point on.
		bool IsThreadStatisticRequired(ThreadSystemID thread_id);

		std::pmr::unsynchronized_pool_resource resource;
		EventQuery query;
		std::u8string query_text;
		// The time window of the query, which is the merged and sorted slices of the time condition. It is
		// empty when the query carries no time condition, and the whole range of the events is used then.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> time_ranges;
		bool has_time_condition = false;
		bool has_context_switch_event = false;
		Potato::Misc::IndexSpan<DurationSec> trace_range;
		// The cores which the events carry, they are collected for every event, before the thread filter.
		std::vector<std::size_t> observed_cores;
		// The result of the thread filter of the threads which are discovered, the key is the system thread id.
		std::pmr::unordered_map<ThreadSystemID, bool> thread_filter;
		std::pmr::unordered_map<ThreadSystemID, ThreadStatistic> thread_statistics;
	};
}
