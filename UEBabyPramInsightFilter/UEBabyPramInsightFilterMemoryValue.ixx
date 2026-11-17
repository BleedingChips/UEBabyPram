module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterMemoryValue;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterMemoryQuery;
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
	// Report the time line of the memory tags which the query selects: the time and the value of every snapshot which
	// carries a tag, in the order of the time, for every memory tag of every tracker and of every query.
	//
	// The query is the query of the memory statistics, and the conditions are read the same way: the Thread condition
	// selects the memory tracker, the chain selects the tag and every tag below it, and the time condition selects the time
	// point of a snapshot, see UEBabyPramInsightFilterMemoryQuery. The difference is what the report carries: the statistic
	// parser aggregates the values of a tag into its first, last, largest, smallest and mean value, and this parser prints
	// every sample of the tag, which is the shape which a chart of the memory of one tag is built from.
	//
	// A tag may be sampled thousands of times, so the report carries Config::max_value_count samples of it by default: the
	// samples are picked by an even time interval from the first sample to the last one, and the sample which is nearest to
	// every point of the interval is the one which is printed, so the printed time line keeps the shape of the values
	// instead of the first N samples of the beginning of the trace. The count is lowered or raised by the command line, and
	// 0 prints every sample, which is what the CSV file of the report is for.
	struct MemoryValueParser : public Parser
	{
		struct Config
		{
			// The queries of the analysis, every one of them is built from its text by ParseEventQuery. The command line
			// collects them from the -mv arguments and reports the error of a query which is not acceptable, so the parser
			// only receives the queries which are ready to be analyzed. The queries are taken over by the parser.
			std::vector<EventQuery> queries;
			// How many samples of one memory tag are printed, the samples are picked by an even time interval. 0 prints every
			// sample.
			std::size_t max_value_count = 100;
			// The path of the CSV file which the time lines are written into instead of the text report, the file is created
			// and its previous content is emptied. Every sample is one row, the value of the row is bytes, and the text report
			// is not printed when the path is set.
			std::optional<std::filesystem::path> csv_path;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One sample of a memory tag: the time point of the snapshot and the value which it sampled for the tag.
		struct ValueSample
		{
			DurationSec time = DurationSec::zero();
			int64 value = 0;
		};

		// The time line of one memory tag of one tracker which one query selects, the samples are kept in the order of the
		// time.
		struct TagTimeLine
		{
			explicit TagTimeLine(std::pmr::memory_resource* resource) : samples(resource) {}
			TagTimeLine(TagTimeLine const&) = default;
			TagTimeLine(TagTimeLine&&) = default;
			TagTimeLine& operator=(TagTimeLine&&) = default;

			std::size_t query_index = 0;
			MemoryTrackerId tracker_id;
			MemoryTagId tag_id;
			std::pmr::vector<ValueSample> samples;
		};

		MemoryValueParser(Config config = {});

		// Store the tag, the tracker and the tag set in the manager of the memory specs, which keeps the association of the
		// id and the name of the spec, and the parent tag and the tag set of a tag.
		virtual void OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name) override;
		virtual void OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name) override;
		virtual void OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name) override;
		// Collect every sample of the tags which the queries select, see the comment of the parser.
		virtual void OnMemoryTagSnap(MemoryTagSnapView snap_view) override;
		// The report resolves the id of every tag and of every tracker to its name, and it reports the value which the
		// snapshots sampled for them, so the specs and the snapshots of the memory channel are both required.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The manager of the specs which the trace declares, the report is written from it.
		MemoryTagsManager const& GetMemoryTagsManager() const { return memory_tags_manager; }
		// The time lines which were collected, they are one entry per query, tracker and memory tag.
		std::span<TagTimeLine const> GetTagTimeLines() const { return tag_time_lines; }

	protected:

		// Write the time lines into the CSV file of Config::csv_path, the file is created or emptied: one row carries one
		// sample, and the value of the row is bytes, so the file is computable.
		bool WriteCSV();

		// Find the time line of the tag of the tracker for the query, and create it when the snapshot is the first one which
		// carries the tag.
		TagTimeLine& FindTagTimeLine(std::size_t query_index, MemoryTrackerId tracker_id, MemoryTagId tag_id);

		// Pick the samples which the report prints: the samples of an even time interval from the first one to the last one,
		// and for every point of the interval the sample which is nearest to it. The picked samples are kept in the order of
		// the time and are at most count of them, and every sample is printed when count is 0 or the time line carries no
		// more samples than it.
		std::pmr::vector<ValueSample> PickSamples(std::span<ValueSample const> samples);

		// The resource of the specs and of the time lines, it is Config::resource and the storages below are allocated from
		// it.
		std::pmr::unsynchronized_pool_resource resource;
		MemoryTagsManager memory_tags_manager;
		std::pmr::vector<TagTimeLine> tag_time_lines;
		// The queries which the time lines are collected by, they are taken over from Config::queries.
		std::pmr::vector<EventQuery> queries;
		// The path of the CSV file of the time lines, it is described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
		// How many samples of one tag are printed, it is Config::max_value_count and 0 prints every sample.
		std::size_t max_value_count = 100;
		// The queries which select the tracker and the time point of the snapshot which is being read, so the walk of the
		// tags of one snapshot only asks the question of the chain. It is reused by every snapshot.
		std::pmr::vector<std::size_t> snapshot_query_indices;
	};
}
