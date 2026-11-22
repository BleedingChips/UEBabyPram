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
	// The report has two modes:
	//   - Peak: every extremum point, which is a sample where the slope changes from positive to negative or from
	//     negative to positive, a slope of 0 is not a turning point. The last peak carries the LastPeak role, and
	//     the first, last, largest and smallest samples are always added.
	//   - All: the time range of the tag is split into Config::max_point_count equal segments, and every segment
	//     prints its first, last, largest and smallest sample with their time, value and slope, followed by the
	//     mean, median and standard deviation of all samples of the segment.
	// A sample which carries more than one role keeps all of them, ex: First/Max or Start/Max, and the points are
	// sorted by time.
	struct MemoryValueParser : public Parser
	{
		// The mode which decides the points between the four special points.
		enum class PointMode : std::uint8_t
		{
			// The extremum points, with the four special points of the whole time line.
			Peak,
			// Equal time segments, every one with its own four points and its statistics.
			All,
		};

		struct Config
		{
			// The queries of the analysis, every one of them is built from its text by ParseEventQuery. The command line
			// collects them from the -mv arguments and reports the error of a query which is not acceptable, so the parser
			// only receives the queries which are ready to be analyzed. The queries are taken over by the parser.
			std::vector<EventQuery> queries;
			// The mode of the points.
			PointMode mode = PointMode::Peak;
			// The Peak mode prints at most this many extremum points of one tag, 0 for no limit. The All mode splits the
			// time range of one tag into this many equal segments, and 0 uses a single segment which covers the whole range.
			std::size_t max_point_count = 20;
			// The Peak mode drops an extremum point whose absolute slope is less than the largest absolute slope of the
			// extremum points of the tag multiplied by this ratio, so the noise of the small turns is not printed. 0
			// keeps every extremum point. The All mode does not read it.
			double min_slope_ratio = 0.05;
			// The path of the CSV file which the time lines are written into instead of the text report, the file is created
			// and its previous content is emptied. The points which the report prints are the rows of the file, and its value
			// is bytes.
			std::optional<std::filesystem::path> csv_path;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One sample of a memory tag: the time point of the snapshot and the value which it sampled for the tag.
		struct ValueSample
		{
			DurationSec time = DurationSec::zero();
			int64 value = 0;
		};

		// The role of a point in the time line. A point carries one or more roles, so a sample which is both the first
		// sample and the largest value carries First and Max.
		enum class PointKind : std::uint16_t
		{
			// A sample where the slope turns from positive to negative or from negative to positive.
			Peak = 1 << 0,
			// A sample of the Peak mode, which is also its last one.
			LastPeak = 1 << 1,
			// The first sample of the tag.
			First = 1 << 2,
			// The last sample of the tag.
			Last = 1 << 3,
			// The first sample of a segment of the All mode.
			SegmentStart = 1 << 4,
			// The last sample of a segment of the All mode.
			SegmentEnd = 1 << 5,
			// The first sample of the largest value.
			Max = 1 << 6,
			// The first sample of the smallest value.
			Min = 1 << 7,
			// A sample of the All mode.
			Sample = 1 << 8,
		};

		static constexpr PointKind CombinePointKind(PointKind left, PointKind right)
		{
			return static_cast<PointKind>(
				static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right)
			);
		}

		static constexpr bool HasPointKind(PointKind flags, PointKind kind)
		{
			return (static_cast<std::uint16_t>(flags) & static_cast<std::uint16_t>(kind)) != 0;
		}

		// One point of the report: the time and the value of a printed sample, the slope of the values at it, which is a size
		// in bytes per second, and the roles of the point in the time line.
		struct PeakPoint
		{
			DurationSec time = DurationSec::zero();
			int64 value = 0;
			double slope = 0.0;
			PointKind kind = PointKind::Sample;
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
		// The conditions of a query which the parser reads: a query which carries another condition is rejected by the
		// command line instead of being accepted with a condition which the parser would silently ignore.
		static constexpr QueryConditionFlag AcceptedQueryCondition()
		{
			return QueryConditionFlag::Time | QueryConditionFlag::Tracker;
		}

		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The manager of the specs which the trace declares, the report is written from it.
		MemoryTagsManager const& GetMemoryTagsManager() const { return memory_tags_manager; }
		// The time lines which were collected, they are one entry per query, tracker and memory tag.
		std::span<TagTimeLine const> GetTagTimeLines() const { return tag_time_lines; }

	protected:

		// Write the points of the report into the CSV file of Config::csv_path, the file is created or emptied: one row
		// carries one point, and the value of the row is bytes, so the file is computable.
		bool WriteCSV();

		// Find the time line of the tag of the tracker, and create it when the snapshot is the first one which carries the tag.
		// The queries only decide whether a tag is selected: a tag which more than one query selects has one time line, not
		// one per query.
		TagTimeLine& FindTagTimeLine(MemoryTrackerId tracker_id, MemoryTagId tag_id);

		// One segment of the All mode: the time range which it covers, its four points (start, end, max and min)
		// and the statistics of every sample in it. The points are sorted by time, and a point which carries more
		// than one role keeps all of them.
		struct SegmentReport
		{
			explicit SegmentReport(std::pmr::memory_resource* resource) : points(resource) {}

			std::size_t index = 0;
			Potato::Misc::IndexSpan<DurationSec> time_range;
			std::size_t sample_count = 0;
			std::pmr::vector<PeakPoint> points;
			// The mean, median and population standard deviation of the values of the segment, in bytes.
			long double mean = 0.0L;
			long double median = 0.0L;
			long double standard_deviation = 0.0L;
		};

		// Pick the extremum points of the Peak mode which the slope filter keeps, limit them by Config::max_point_count,
		// add the four special points (first, last, max and min), and mark the last printed peak. When extremum_count is
		// not null, it is set to the count of every extremum point of the time line before the filter and the limit, so
		// the report tells how many extremum points the tag carries. The points are kept in the order of the time, and
		// a point which carries more than one role keeps all of them.
		std::pmr::vector<PeakPoint> PickPoints(std::span<ValueSample const> samples, std::size_t* extremum_count = nullptr);

		// Build the segments of the All mode: the time range of the samples is split into Config::max_point_count
		// equal segments (a single segment when the count is 0), and every segment keeps its start, end, max and min
		// points and the mean, median and standard deviation of its samples.
		std::pmr::vector<SegmentReport> BuildSegments(std::span<ValueSample const> samples);

		// The resource of the specs and of the time lines, it is Config::resource and the storages below are allocated from
		// it.
		std::pmr::unsynchronized_pool_resource resource;
		MemoryTagsManager memory_tags_manager;
		std::pmr::vector<TagTimeLine> tag_time_lines;
		// The queries which the time lines are collected by, they are taken over from Config::queries.
		std::pmr::vector<EventQuery> queries;
		// The path of the CSV file of the time lines, it is described by Config::csv_path.
		std::optional<std::filesystem::path> csv_path;
		// The mode of the points, it is Config::mode.
		PointMode mode = PointMode::Peak;
		// How many points of one tag a mode may print, it is Config::max_point_count and 0 prints every selected point.
		std::size_t max_point_count = 20;
		// The ratio of the largest absolute slope below which an extremum point is dropped, it is Config::min_slope_ratio.
		double min_slope_ratio = 0.05;
		// Whether the tracker and the time point of the snapshot which is being read are selected by any query, which decides
		// whether the walk of the tags of that snapshot asks the question of the chain at all.
		bool is_snapshot_selected = false;
	};
}
