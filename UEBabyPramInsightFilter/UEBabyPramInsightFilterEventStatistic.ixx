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
	// During the analysis every event id of every chain element is accumulated into its own record, which keeps no name,
	// and the records are aggregated into the statistics of the names in AllAnalyzeDone(): every query keeps its own
	// statistics, the events which carry the same name share one statistic inside one query, and a merged element
	// reports one statistic of the element itself. An id which is discovered but never occurs keeps no record, so it
	// is not reported.
	//
	// The matches of one query inside one stack tree are collapsed before they are counted: two matches of the same
	// event whose time ranges overlap are the same call seen from two chain elements, so only the longest of them is
	// counted and the shorter ones are ignored. The matches of two queries are never compared.
	//
	// Every counted occurrence is accumulated into the record of its event id:
	//   - the occurrence count, the total duration and the total exclude time (the duration without
	//     the time spent in the child events),
	//   - the median and the standard deviation of the duration of the single occurrence, which are
	//     built from the stored occurrences in AllAnalyzeDone(), when the records are merged by name,
	//   - the time ranges of the top N (Config::top_count) longest occurrences, with the exclude time
	//     of every one of them and its exclude time without the context switch time, which are merged
	//     by name and completed in AllAnalyzeDone() when the context switch events are complete.
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
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
			// How many buckets the histogram of the duration of the single occurrence carries, it is only built when the
			// count of the occurrences of an event is greater than top_count, and the range from the shortest duration to
			// the longest one is split into this count of buckets.
			std::size_t duration_bucket_count = 5;
			// The path of the CSV file which the statistics are written into instead of the text report, the file is
			// created and its previous content is emptied. Every statistic which carries an occurrence is one row of
			// the file, the content is UTF-8 and the fields follow RFC 4180 (a field which carries the delimiter, the
			// quote or a line break is quoted, and the quotes inside it are doubled), so a spreadsheet or a script can
			// read it. The text report is not printed when the path is set.
			std::optional<std::filesystem::path> csv_path;
		};

		// One of the longest occurrences of an event name, the time range of the occurrence, its
		// exclude time, the context switch time, the exclude time without the context switch time
		// and the thread which the occurrence happened on are kept, so that the occurrence can be
		// reported.
		struct TopEventOccurrence
		{
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
		};

		// The accumulated timing of one event id of one chain element of one query: the records are keyed by the query,
		// the element and the id of the event, so every id is counted separately, and the report statistics are built
		// from them by name in AllAnalyzeDone(). The record keeps no name of the event, the name is resolved when the
		// records are aggregated.
		struct EventStatisticRecord
		{
			EventStatisticRecord(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: durations(resource), occurrence_time_ranges(resource), top_occurrences(resource) {}

			std::size_t query_index = 0;
			// The index of the chain element which counts the event, it tells the kind of the element: a normal element
			// is reported by the name of the event, a merged element is reported by the element itself.
			std::size_t node_index = 0;
			// The event which the record counts, every id has its own record.
			EventID event_id;
			// The count of the occurrences of the event.
			std::size_t count = 0;
			DurationSec total_duration = DurationSec::zero();
			DurationSec total_exclude_time = DurationSec::zero();
			// One duration per occurrence, they are merged into the statistic of the event name in AllAnalyzeDone().
			std::pmr::vector<DurationSec> durations;
			// The time ranges of the occurrences, they are merged into the statistic of the event name in AllAnalyzeDone().
			std::pmr::vector<Potato::Misc::IndexSpan<DurationSec>> occurrence_time_ranges;
			// The buffered top occurrences of the id, they are merged into the statistic of the event name in AllAnalyzeDone().
			std::pmr::vector<TopEventOccurrence> top_occurrences;
			// The shortest duration of the buffered occurrences, 0 while the buffer has never been sorted: an occurrence
			// which is shorter than it can not be one of the longest ones, so it never enters the buffer.
			DurationSec min_top_occurrence_duration = DurationSec::zero();
		};

		// The statistic of one event name of one query, or of one merged element of one query. It is built in
		// AllAnalyzeDone() from the records of the ids which carry the name, and it is what the report prints.
		// Every occurrence of the name is accumulated here, and the time ranges of the longest occurrences are kept,
		// so that the occurrences which cost the most time can be reported.
		struct EventStatistic
		{
			// One bucket of the histogram of the duration of the single occurrence: the range of the durations which it
			// carries, how many occurrences fell into it, and the share of them over the whole count.
			struct DurationDistributionBucket
			{
				// The range of the durations of the bucket.
				Potato::Misc::IndexSpan<DurationSec> duration_range;
				// How many occurrences fell into the bucket.
				std::size_t count = 0;
				// The share of the bucket over the whole count of the occurrences, in percent.
				double percent = 0.0;
			};

			// The statistics of one count with which the event occurred inside one game frame: the frames which carry that
			// count, and the mean of the total time which the event took inside those frames.
			struct FrameCountStatistic
			{
				// The count of the occurrences of the event inside one game frame.
				std::size_t count = 0;
				// How many game frames carry that count.
				std::size_t frame_count = 0;
				// The mean of the total time which the event took inside such a frame.
				DurationSec mean_duration = DurationSec::zero();
				// The game frame which carries that count and in which the event took the most time, and that time.
				Potato::Misc::IndexSpan<DurationSec> longest_frame_time_range;
				DurationSec longest_frame_duration = DurationSec::zero();
			};

			EventStatistic(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_name(resource), event_ids(resource), durations(resource), occurrence_time_ranges(resource), frame_count_statistics(resource), duration_distribution(resource), top_occurrences(resource) {}

			// The index of the query which counts the event, see EventQueryMapping::GetQueries(). Together with the name
			// of the event it is the key of the statistic: one name keeps one statistic inside one query, and two queries
			// never share a statistic.
			std::size_t query_index = 0;
			// The ids of the events which carry the name of the statistic, the ids are collected when the records are
			// aggregated, and the report tells how many of them the name belongs to.
			std::pmr::vector<EventID> event_ids;
			// Whether the statistic merges every event which its chain element matches: the name of such a statistic is
			// the regular expression of the element, and the ids of all matched events are counted together.
			bool is_merged = false;
			// The name of the event, or the regular expression of the merged element, it is resolved in AllAnalyzeDone()
			// when the records are aggregated.
			std::pmr::u8string event_name;
			// The count of the occurrences of the event.
			std::size_t count = 0;
			// One duration per occurrence, the durations are kept to build the median and the
			// standard deviation of the duration of the single occurrence in AllAnalyzeDone().
			std::pmr::vector<DurationSec> durations;
			// The time ranges of the occurrences, they are what the occurrences are grouped into the game frames by, the
			// game frame of an occurrence is the one which carries its beginning.
			std::pmr::vector<Potato::Misc::IndexSpan<DurationSec>> occurrence_time_ranges;
			// The statistics which are merged by the count of the occurrences inside one game frame, the order is the
			// ascending order of the count.
			std::pmr::vector<FrameCountStatistic> frame_count_statistics;
			// The histogram of the duration of the single occurrence, it is built in AllAnalyzeDone() when the count of the
			// occurrences is greater than Config::top_count: the range from the shortest duration to the longest one is split
			// into Config::duration_bucket_count buckets.
			std::pmr::vector<DurationDistributionBucket> duration_distribution;
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
			// exclude time and its exclude time without the context switch time. The occurrences are buffered: the
			// buffer is only sorted when it carries more than top_count plus half of top_count (capped at 200)
			// occurrences, and it is cut to top_count when the analysis is done, so exactly the longest ones are
			// reported.
			std::pmr::vector<TopEventOccurrence> top_occurrences;
			// The shortest duration of the buffered occurrences, 0 while the buffer has never been sorted: an occurrence
			// which is shorter than it can not be one of the longest ones, so it never enters the buffer.
			DurationSec min_top_occurrence_duration = DurationSec::zero();
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
		// The conditions of a query which the parser reads: a query which carries another condition is rejected by the
		// command line instead of being accepted with a condition which the parser would silently ignore.
		static constexpr QueryConditionFlag AcceptedQueryCondition()
		{
			return QueryConditionFlag::Time | QueryConditionFlag::Thread;
		}

		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The statistics of every event of every query, keyed by EventStatistic::query_index and by the id of the
		// event, the order is unspecified.
		std::span<EventStatistic const> GetEventStatistics() const { return event_statistics; }

		// The query system which is built from Config::queries, it resolves the regular expression of
		// every chain element into the ids of the events and it tells which occurrences are counted.
		EventQueryMapping const& GetQueryMapping() const { return query_mapping; }

	protected:

		// Find or create the record of one event of one chain element of one query: the record is keyed by the query,
		// the element and the id of the event, so every id is counted separately and the record keeps no name of the
		// event. The records are aggregated by name in AllAnalyzeDone().
		EventStatisticRecord& AddEventStatisticRecord(std::size_t query_index, std::size_t node_index, EventID id);
		// Record one occurrence of the event of the record: the occurrence count, the total
		// duration and the total exclude time are accumulated, the sample of the occurrence is
		// stored for the median and the standard deviation, and the time range of the occurrence is
		// kept with its thread and its exclude time when it enters the buffered top N of the record.
		void RecordEventOccurrence(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, DurationSec exclude_time, EventStatisticRecord& record);
		// Build the statistics which are merged by the count of the occurrences inside one game frame, and find the game
		// frame in which the event took the most time, they are built in AllAnalyzeDone() from the frames which the
		// recorder of the game frames collected.
		void BuildFrameStatistic(EventStatistic& statistic);
		// Build the histogram of the duration of the single occurrence when the count of the occurrences is greater than
		// top_count: the range from the shortest duration to the longest one is split into duration_bucket_count buckets,
		// and every bucket keeps how many occurrences fell into it and the share of them over the whole count.
		void BuildDurationDistribution(EventStatistic& statistic);
		// Print the timing of one event, the name is the copy which the statistic carries.
		void PrintEventStatistic(Potato::Log::LogPrinter& printer, EventStatistic const& statistic) const;
		// Write every statistic which carries an occurrence into the CSV file of Config::csv_path, the file is
		// created or emptied. One row carries one statistic, and the rows are ordered by the query and by the total
		// duration of the statistic, the longest one first. The header of the file names the columns.
		bool WriteCSV() const;
		// Write one query back into the text of the query grammar: the regular expression of every element with the
		// delimiters of its kind, and the thread and the time conditions, so the text can be used as an input query
		// again. The text is also reported with the query and with every row of the CSV file.
		std::wstring FormatQueryText(std::size_t query_index) const;

		std::pmr::unsynchronized_pool_resource resource;
		// The streamer of the context switch events, the blocks of the events are streamed to the listeners which
		// every top occurrence creates for the time range of its stack tree, and the occurrence reads the time which
		// its thread spent out of the CPU from the blocks which its listener collected.
		ContextSwitchStreamer context_switch_streamer;
		// The recorder of the game frames: the events which begin a frame and the game thread itself are recorded by it,
		// and the time range of every frame which the analysis meets is kept, so an occurrence is grouped into the frame
		// which carries its beginning.
		GameFrameRecoder game_frame_recoder;
		// The events manager dedups the discovered events and keeps their names, so the query system is asked about
		// every event once, and the name of an event is resolved from it when the stack tree creates its statistic.
		EventSpecManager events_manager;
		// The query system which is built from Config::queries, the ids of the events are resolved
		// into it when they are discovered, so the stack tree is never matched by a name.
		EventQueryMapping query_mapping;
		// The accumulated timing of every event id of every chain element, keyed by the query, the element and the
		// id of the event. The records keep no name, and they are aggregated by name in AllAnalyzeDone().
		std::pmr::vector<EventStatisticRecord> event_statistic_records;
		// The index of the record of a (query, element, event id), so an occurrence finds its record without a walk.
		std::map<std::tuple<std::size_t, std::size_t, std::size_t>, std::size_t> event_statistic_record_indices;
		// The statistics of every event name of every query, or of every merged element, they are built from the
		// records in AllAnalyzeDone() and they are what the report prints.
		std::pmr::vector<EventStatistic> event_statistics;

		std::size_t top_count = 5;
		// The count of the buffered occurrences which triggers a sort and a trim: every statistic keeps the occurrences
		// until its buffer carries top_count plus the slack, which is half of top_count capped at 200, and only a buffer
		// which is over it is sorted, so the sort is paid for once per slack kept occurrences instead of once per
		// occurrence.
		std::size_t top_occurrence_buffer_limit = 5;
		// The count of the buckets of the histogram of the duration of the single occurrence.
		std::size_t duration_bucket_count = 5;
		// The path of the CSV file which the statistics are written into instead of the text report, it is
		// described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
	};
}