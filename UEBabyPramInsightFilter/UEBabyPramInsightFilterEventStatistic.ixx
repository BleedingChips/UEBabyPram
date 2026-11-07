module;

#include <cassert>

export module UEBabyPramInsightFilterEventStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserContextSwitchStreamer;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Collect the timing of the events which the event queries match.
	//
	// Every query is a call chain (see EventQuery and GetFilterEbnf()), and the chain elements which are
	// marked as statistic ([^Regex^]) are the events whose timing is collected: an occurrence of such an
	// element is counted when the whole chain is completed with it, which means that the preceding
	// elements of the chain are its ancestors and the following ones are its descendants, in the order
	// of the call (see EventQueryMapping).
	//
	// The statistics are keyed by the query which counts the event and by the name of the event: every
	// query keeps its own statistics, so the same name is counted independently by two queries, and one
	// name keeps one statistic inside one query.
	//
	// The matches of one query inside one stack tree are collapsed before they are counted: two matches
	// whose time ranges overlap are the same call seen from two chain elements, so only the longest of
	// them is counted and the shorter ones are ignored. The matches of two queries are never compared.
	//
	// Every counted occurrence is accumulated into the statistic of its event name:
	//   - the occurrence count, the total duration and the total exclude time (the duration without
	//     the time spent in the child events),
	//   - the median and the standard deviation of the duration of the single occurrence, which are
	//     built from the stored occurrences in AllAnalyzeDone(),
	//   - the time ranges of the top N (Config::top_count) longest occurrences, with the exclude time
	//     of every one of them and its exclude time without the context switch time, which is built
	//     in AllAnalyzeDone() when the context switch events are complete,
	//   - the ancestors and the direct children of every top occurrence, with the exclude time of
	//     every one of them in that occurrence.
	// One event name may be carried by several event ids, the ids which carry a name are merged into
	// the statistic of that name, so every name is counted separately.
	struct EventStatisticParser : public Parser
	{
		struct Config
		{
			// The queries of the analysis, every one of them is built from its text by ParseEventQuery,
			// ex: std::get<EventQuery>(ParseEventQuery(u8"(^Tick^)->[^Update^]")). The command line
			// collects them from the -es arguments, and it reports the error of a query which is not
			// acceptable, so the parser only receives the queries which are ready to be analyzed.
			// Every query keeps its own statistics, and the same event name is counted independently by
			// two queries, so the same name may be reported twice when it is matched by two queries.
			// The queries are taken over by the parser.
			std::vector<EventQuery> queries;
			// How many of the longest occurrences of every event name are kept as a time range,
			// 0 for none.
			std::size_t top_count = 5;
			// The maximum depth difference between the top occurrence and the parent and child events
			// which are kept with it, 0 for none. The ancestors above the occurrence and the descendants
			// below it are only kept when their depth difference is not greater than this value, the
			// events beyond it are neither recorded nor reported.
			std::size_t max_context_depth = 6;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One event of the call context of an occurrence: the event itself, the time which it consumed in
		// the occurrence, its exclude time and its context switch time. The event is kept by id, the name
		// is resolved with the events manager when the occurrence is reported.
		struct ContextEvent
		{
			EventID event_id;
			// The whole time which the event consumed in the occurrence, i.e. the size of its time range.
			DurationSec duration = DurationSec::zero();
			// The duration of the event without the time spent in its child events.
			DurationSec exclude_time = DurationSec::zero();
			// The time which the thread spent out of the CPU in the whole time range of the event, it is
			// built in AllAnalyzeDone(), when the context switch events are complete.
			DurationSec context_switch_time = DurationSec::zero();
			// The whole time range of the event in the stack tree, it is kept to resolve the context
			// switch time of the event in AllAnalyzeDone().
			Potato::Misc::IndexSpan<DurationSec> time_range;
			// The depth of the event in the stack tree.
			std::size_t depth = 0;
			// The placeholder of the descendants which are beyond the depth limit, it carries no
			// information and it is printed as an ellipsis.
			bool is_ellipsis = false;
		};

		// One of the longest occurrences of an event name, the time range of the occurrence, its
		// exclude time, the context switch time, the exclude time without the context switch time,
		// the thread which the occurrence happened on and the call context of the occurrence are
		// kept, so that the occurrence can be reported.
		struct TopEventOccurrence
		{
			TopEventOccurrence(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: parent_events(resource), child_events(resource) {}

			Potato::Misc::IndexSpan<DurationSec> time_range;
			// The depth of the occurrence in the stack tree.
			std::size_t depth = 0;
			// The duration of the occurrence without the time spent in the child events.
			DurationSec exclude_time = DurationSec::zero();
			// The time which the thread spent out of the CPU in the whole time range of the occurrence,
			// which is the context switch time of the occurrence. It is built in AllAnalyzeDone(), when
			// the context switch events are complete.
			DurationSec context_switch_time = DurationSec::zero();
			// The exclude time without the time which the thread was switched out of the CPU in the
			// whole time range of the occurrence. It is initialized to the exclude time, and the time
			// out of the CPU is removed in AllAnalyzeDone(), when the context switch events are
			// complete.
			DurationSec exclude_time_without_context_switch = DurationSec::zero();
			// The thread which the occurrence happened on. The id is kept instead of the name, the
			// name is resolved from the thread info in PrintToLog, so the occurrence does not have
			// to own a copy of the name.
			ThreadID thread_id;
			// The listener which collects the context switch blocks of the thread for the occurrence: it is created
			// with the time range of the stack tree which the occurrence is in, and it is the source of the context
			// switch time of the occurrence.
			std::shared_ptr<ContextSwitchStreamer::Listener> context_switch_listener;
			// The ancestors of the occurrence, from the outermost one to the direct parent, the call
			// order is kept so that the chain can be reported.
			std::pmr::vector<ContextEvent> parent_events;
			// The descendants of the occurrence which are not deeper than the depth limit, in the
			// order of the call, a placeholder stands for the omitted deeper descendants.
			std::pmr::vector<ContextEvent> child_events;
		};

		// The statistics of one event name of one query, which is the key of the statistic.
		// The same event name may be carried by several event ids (it may be declared in several
		// places, and the event of the CPU scope logger is a separate id as well), so the ids which
		// carry the name are kept with the statistic. The name itself is not stored, it is taken
		// from the events manager with the first id of the statistic.
		// Every occurrence of one of the ids is accumulated here, and the time ranges of the longest
		// occurrences are kept, so that the occurrences which cost the most time can be reported.
		struct EventStatistic
		{
			EventStatistic(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_ids(resource), durations(resource), top_occurrences(resource) {}

			// The index of the query which counts the events of this statistic, see
			// EventQueryMapping::GetQueries(). Together with the name of the event it is the key of the
			// statistic: one name keeps one statistic inside one query, and two queries never share a
			// statistic.
			std::size_t query_index = 0;
			// The ids of the events which carry this name, the occurrences of all of them are merged
			// into this statistic. The ids are the ones which the parser receives and the ones
			// which are carried by the stack tree of the CPU event, the id of the event of the CPU
			// scope logger is inverted, the events manager resolves it.
			std::pmr::vector<EventID> event_ids;
			// The count of the occurrences of this name.
			std::size_t count = 0;
			// One duration per occurrence, the durations are kept to build the median and the
			// standard deviation of the duration of the single occurrence in AllAnalyzeDone().
			std::pmr::vector<DurationSec> durations;
			// The total duration of the occurrences, which is the sum of the duration of every
			// single occurrence.
			DurationSec total_duration = DurationSec::zero();
			// The total duration of the occurrences without the time spent in the child events.
			DurationSec total_exclude_time = DurationSec::zero();
			// The median of the duration of the single occurrence, which is built in AllAnalyzeDone().
			DurationSec median_duration = DurationSec::zero();
			// The standard deviation of the duration of the single occurrence, which is the square
			// root of the variance of the durations, it is built in AllAnalyzeDone().
			DurationSec duration_standard_deviation = DurationSec::zero();
			// The top N (Config::top_count) longest occurrences, each one keeps its time range, its
			// exclude time and its exclude time without the context switch time, the shortest one is
			// kicked out when the list is full and a longer occurrence is found, the order is
			// unspecified.
			std::pmr::vector<TopEventOccurrence> top_occurrences;
		};

		EventStatisticParser(Config config = {});

		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		// Only the threads which at least one query counts are analyzed: a query which carries a thread
		// condition only counts the threads which its matcher matched, so a thread which is matched by no
		// query carries no event which has to be counted. The thread is known by its thread id and by its
		// system thread id, so both of them are answered.
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		virtual bool IsThreadRequired(ThreadSystemID thread_system_id) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The statistics of every event name of every query, keyed by EventStatistic::query_index and by
		// the name of the event, the order is unspecified.
		std::span<EventStatistic const> GetEventStatistics() const { return event_statistics; }

		// The query system which is built from Config::queries, it resolves the regular expression of
		// every chain element into the ids of the events and it tells which occurrences are counted.
		EventQueryMapping const& GetQueryMapping() const { return query_mapping; }

	protected:

		// Find the statistic of the event name which the id carries inside the query, and create it when
		// the name is not counted by the query yet. The event has to be stored in the events manager
		// before this is called, the name is resolved from the id, and the id is only appended to the
		// statistic once.
		EventStatistic& GetNameStatistic(std::size_t query_index, EventID id);
		// Record one occurrence of the event of the statistic: the occurrence count, the total
		// duration and the total exclude time are accumulated, the sample of the occurrence is
		// stored for the median and the standard deviation, and the time range of the occurrence is
		// kept with its thread and its exclude time when it ranks in the top N of the statistic.
		void RecordEventOccurrence(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, DurationSec exclude_time, EventStatistic& statistic);
		// Print the timing of one event name, the name is resolved from the events manager.
		void PrintEventStatistic(Potato::Log::LogPrinter& printer, EventStatistic const& statistic) const;

		std::pmr::unsynchronized_pool_resource resource;
		// The streamer of the context switch events, the blocks of the events are streamed to the listeners which
		// every top occurrence creates for the time range of its stack tree, and the occurrence reads the time which
		// its thread spent out of the CPU from the blocks which its listener collected.
		ContextSwitchStreamer context_switch_streamer;
		EventSpecManager events_manager;
		// The query system which is built from Config::queries, the ids of the events are resolved
		// into it when they are discovered, so the stack tree is never matched by a name.
		EventQueryMapping query_mapping;
		// The statistics of every event name of every query, keyed by the query and by the name.
		std::pmr::vector<EventStatistic> event_statistics;

		std::size_t top_count = 5;
		// The maximum depth difference between the top occurrence and the parent and child events which
		// are kept with it, 0 for none.
		std::size_t max_context_depth = 6;
	};
}
