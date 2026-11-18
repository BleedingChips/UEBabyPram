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
	// The queries only decide which tags are reported: a tag which more than one query selects is reported once, so the
	// report has no index of the query and the rows are ordered by the tracker and then by the tag.
	//
	// The query is the query of the memory statistics, and the conditions are read the same way: the Thread condition
	// selects the memory tracker, the chain names the tag by its parent chain, and the time condition selects the time
	// point of a snapshot, see UEBabyPramInsightFilterMemoryQuery. The difference is what the report carries: the statistic
	// parser aggregates the values of a tag into its first, last, largest, smallest and mean value, and this parser prints
	// the shape of the time line of the tag, which is what a chart of the memory of one tag is built from.
	//
	// A tag may be sampled thousands of times, so the report does not print every sample: it prints the first sample, the
	// last sample and the peaks between them, which are the samples where the slope of the values turns, i.e. where it goes
	// from positive to negative, from negative to positive, or to zero. A peak which is less than 5% away from the peak
	// before it, measured against the larger of the two values, is a small wave which the report drops, so the shape of the
	// tag is not hidden by the noise of the memory allocator. The first and the last sample are always printed.
	//
	// Config::max_peak_count limits how many peaks of one tag are printed, the largest ones are kept by an even pick of the
	// peaks, and Config::min_peak_sample_count keeps two printed peaks at least that many samples apart, so a tag which
	// oscillates around one value is not reported as thousands of peaks. Config::all_samples prints every sample of the tag
	// instead, which is the mode of the whole time line, and the two limits do not apply to it.
	struct MemoryValueParser : public Parser
	{
		struct Config
		{
			// The queries of the analysis, every one of them is built from its text by ParseEventQuery. The command line
			// collects them from the -mv arguments and reports the error of a query which is not acceptable, so the parser
			// only receives the queries which are ready to be analyzed. The queries are taken over by the parser.
			std::vector<EventQuery> queries;
			// How many peaks of one memory tag are printed, the peaks are picked evenly when the tag carries more of them than
			// this count. 0 prints every peak.
			std::size_t max_peak_count = 20;
			// How many samples are between two printed peaks at least: a peak which is fewer samples after the previous printed
			// peak is dropped, which keeps a tag which oscillates around one value from being reported as thousands of peaks.
			// 0 drops no peak for the distance.
			std::size_t min_peak_sample_count = 5;
			// Whether every sample of a tag is printed instead of the first sample, the peaks and the last one: it is the mode
			// which exports the whole time line, ex: for a chart which is drawn from every sample, and it is what the CSV file
			// of a long time line is for. The limits of the peaks do not apply to it.
			bool all_samples = false;
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

		// Where a point of the report is in the time line of the tag.
		enum class PointKind : std::uint8_t
		{
			// The first sample of the tag, which is always printed.
			First,
			// A sample where the slope of the values turned, which is a peak or a valley of the time line.
			Peak,
			// A sample which is neither the first nor the last one, which is only printed by the mode which prints every
			// sample of the tag.
			Sample,
			// The last sample of the tag, which is always printed.
			Last,
		};

		// One point of the report: the time and the value of a printed sample, the slope of the values at it, which is a size
		// in bytes per second, and where the point is in the time line.
		struct PeakPoint
		{
			DurationSec time = DurationSec::zero();
			int64 value = 0;
			double slope = 0.0;
			PointKind kind = PointKind::Peak;
		};

		// The time line of one memory tag of one tracker which one query selects, the samples are kept in the order of the
		// time.
		struct TagTimeLine
		{
			explicit TagTimeLine(std::pmr::memory_resource* resource) : samples(resource) {}
			TagTimeLine(TagTimeLine const&) = default;
			TagTimeLine(TagTimeLine&&) = default;
			TagTimeLine& operator=(TagTimeLine&&) = default;

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

		// Find the time line of the tag of the tracker, and create it when the snapshot is the first one which carries the tag.
		// The queries only decide whether a tag is selected: a tag which more than one query selects has one time line, not
		// one per query.
		TagTimeLine& FindTagTimeLine(MemoryTrackerId tracker_id, MemoryTagId tag_id);

		// Pick the points which the report prints: the first sample of the tag, the peaks between them and the last sample.
		// A peak is a sample where the slope of the values turns, a peak which is less than 5% away from the previous peak is
		// dropped, and the peaks are limited by Config::max_peak_count and spaced by Config::min_peak_sample_count. The
		// points are kept in the order of the time.
		std::pmr::vector<PeakPoint> PickPeaks(std::span<ValueSample const> samples);

		// The resource of the specs and of the time lines, it is Config::resource and the storages below are allocated from
		// it.
		std::pmr::unsynchronized_pool_resource resource;
		MemoryTagsManager memory_tags_manager;
		std::pmr::vector<TagTimeLine> tag_time_lines;
		// The queries which the time lines are collected by, they are taken over from Config::queries.
		std::pmr::vector<EventQuery> queries;
		// The path of the CSV file of the time lines, it is described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
		// How many peaks of one tag are printed, it is Config::max_peak_count and 0 prints every peak.
		std::size_t max_peak_count = 20;
		// How many samples are between two printed peaks at least, it is Config::min_peak_sample_count.
		std::size_t min_peak_sample_count = 5;
		// Whether every sample of a tag is printed, it is Config::all_samples.
		bool all_samples = false;
		// Whether the tracker and the time point of the snapshot which is being read are selected by any query, which decides
		// whether the walk of the tags of that snapshot asks the question of the chain at all.
		bool is_snapshot_selected = false;
	};
}
