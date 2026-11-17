module;

export module UEBabyPramInsightFilterMemoryStatistic;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserMemoryTagsManager;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Collect the memory tag snapshots of an insight trace and report the value statistics of the memory tags which the
	// query selects.
	//
	// The LLM channel of the trace declares the memory tags, the trackers and the tag sets by their id, while a memory
	// sample only carries the ids, so the specs are the only place where the name of an id is found. The specs and their
	// association, which is the parent tag and the tag set of a tag, are stored by MemoryTagsManager, and the parser only
	// forwards the callbacks of the trace to the manager and reports what the manager carries.
	//
	// Both ParserRequireFlag::MEMORY_TAG and ParserRequireFlag::MEMORY_VALUE are required, which is what makes the memory
	// analyzer of the trace parser register the specs of the tags, of the trackers and of the tag sets, and the snapshots
	// which carry the values. The CPU event, the event spec, the context switch and the meta data routes are not required,
	// so the parser reads no event of them.
	//
	// The query is the query of the event statistics, and three things are read differently because the memory data is
	// shaped differently from the call stacks:
	//   - The Thread condition selects the memory trackers: a snapshot is produced by one tracker, so the condition is
	//     matched against the name of the tracker, and a query which carries no Thread condition selects every tracker.
	//   - The call chain is matched against the parent chain of the memory tag: a tag is named by the segments of its
	//     ancestors, ex: A/B/C, so every element of the chain is matched against one segment from the root, and the tags
	//     which the query selects are the tag of the last element of the chain and every tag below it, ex: the chain
	//     (^A^) -> (^B^) selects A/B, A/B/C, A/B/C/D and so on. The element which matches any event, %, matches any
	//     segment, and a tag whose name carries fewer segments than the chain is never selected.
	//   - The time condition is applied to the time point of a snapshot instead of the time range of an occurrence: the
	//     time of a snapshot is a single point, so Time selects the snapshots which are inside one of its slices or on the
	//     boundary of one, and ITime selects the snapshots which are strictly inside one of its slices, which is a part of
	//     the slices and may select no snapshot at all.
	//
	// The statistics of one memory tag are kept per tracker and per query, because every tracker samples the tag and the
	// same tracker may be selected by more than one query. Every unit is reported with the first and the last snapshot
	// which carries it, the snapshots of its largest and of its smallest value, and the mean of the values of all of its
	// snapshots in the selected range.
	struct MemoryStatisticParser : public Parser
	{
		// The order in which the statistics are reported.
		enum class SortKind : std::uint8_t
		{
			// The order of the data: the query which accumulated the statistic, the name of the tracker and the name of the
			// tag. It is the default, and the report of the same trace is always the same in it.
			Name,
			// The mean of the values of the statistic, the largest one first.
			Mean,
			// The largest value which the tag was sampled with, the largest one first.
			Max,
		};

		struct Config
		{
			// The queries of the analysis, every one of them is built from its text by ParseEventQuery, ex:
			// std::get<EventQuery>(ParseEventQuery(u8"(^A^)->(^B^)&Thread=(^.*Default.*^)&Time=[0s,10s]")). The command
			// line collects them from the -ms arguments and reports the error of a query which is not acceptable, so the
			// parser only receives the queries which are ready to be analyzed. The queries are taken over by the parser.
			std::vector<EventQuery> queries;
			// The path of the CSV file which the statistics are written into instead of the text report, the file is created
			// and its previous content is emptied. Every statistic is one row, the values of the row are bytes, and the text
			// report is not printed when the path is set.
			std::optional<std::filesystem::path> csv_path;
			// The order of the report, see SortKind.
			SortKind sort = SortKind::Name;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The statistics of one memory tag of one tracker which one query selects, they are accumulated from the snapshots
		// in the order of their time.
		struct TagStatistic
		{
			std::size_t query_index = 0;
			MemoryTrackerId tracker_id;
			MemoryTagId tag_id;

			// The first and the last snapshot which carries the tag, with the value of that snapshot.
			DurationSec first_time = DurationSec::zero();
			int64 first_value = 0;
			DurationSec last_time = DurationSec::zero();
			int64 last_value = 0;

			// The snapshot of the largest and of the smallest value which the tag was sampled with.
			DurationSec max_time = DurationSec::zero();
			int64 max_value = std::numeric_limits<int64>::min();
			DurationSec min_time = DurationSec::zero();
			int64 min_value = std::numeric_limits<int64>::max();

			// The count of the snapshots which carried the tag and the sum of their values, which the mean is computed
			// from. The sum is wider than the value, so a long trace does not overflow it.
			std::size_t count = 0;
			long double sum = 0.0L;
		};

		MemoryStatisticParser(Config config = {});

		// Store the tag, the tracker and the tag set in the manager of the memory specs, which keeps the association of
		// the id and the name of the spec, and the parent tag and the tag set of a tag. The same id is only stored once,
		// the spec which is stored first is kept.
		virtual void OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name) override;
		virtual void OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name) override;
		virtual void OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name) override;
		// Accumulate every tag of the snapshot which the queries select, see the comment of the parser: the tracker is
		// selected by the Thread condition, the tag by the chain, and the time point by the time condition.
		virtual void OnMemoryTagSnap(MemoryTagSnapView snap_view) override;
		// The memory analyzer registers the LLM routes only when the memory channel is required, and no CPU event, event
		// spec, context switch or meta data is needed by this parser.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The manager of the specs which the trace declares, the report is written from it.
		MemoryTagsManager const& GetMemoryTagsManager() const { return memory_tags_manager; }
		// The statistics which were accumulated, they are one entry per query, tracker and memory tag, and the report
		// prints them ordered by the name of the tracker and of the tag.
		std::span<TagStatistic const> GetTagStatistics() const { return tag_statistics; }

	protected:

		// Write the statistics into the CSV file of Config::csv_path, the file is created or emptied: one row carries one
		// statistic, ordered by the query, the tracker and the tag, and the values of the row are bytes, so the file is
		// computable and the unit is the name of the column.
		bool WriteCSV();

		// Find the statistics of the tag of the tracker for the query, and create them when the snapshot is the first one
		// which carries the tag.
		TagStatistic& FindTagStatistic(std::size_t query_index, MemoryTrackerId tracker_id, MemoryTagId tag_id);

		// The resource of the specs and of the statistics, it is Config::resource and the storages below are allocated
		// from it.
		std::pmr::unsynchronized_pool_resource resource;
		MemoryTagsManager memory_tags_manager;
		std::pmr::vector<TagStatistic> tag_statistics;
		// The queries which the statistics are accumulated by, they are taken over from Config::queries.
		std::pmr::vector<EventQuery> queries;
		// The path of the CSV file of the statistics, it is described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
		// The order of the report, it is Config::sort.
		SortKind sort = SortKind::Name;
		// The queries which select the tracker and the time point of the snapshot which is being read, so the walk of the
		// tags of one snapshot only asks the question of the chain. It is reused by every snapshot, so reading a trace with
		// millions of snapshots does not allocate once per snapshot.
		std::pmr::vector<std::size_t> snapshot_query_indices;
	};
}
