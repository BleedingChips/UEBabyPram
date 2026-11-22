module;

#include <re2/re2.h>

module UEBabyPramInsightFilterMemoryValue;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterMemoryQuery;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserMemoryTagsManager;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// The name of a spec which the trace declares with an empty name is reported as unknown instead of an empty pair of
		// angle brackets.
		std::u8string_view DisplayName(std::u8string_view name)
		{
			return name.empty()
				? std::u8string_view{ u8"<Unknown>" }
				: name;
		}

		// One row of the report: the time line of a tag with the names which the manager resolves for it, so the sort of the
		// report compares the names instead of looking them up again.
		struct ReportRow
		{
			MemoryValueParser::TagTimeLine const* time_line = nullptr;
			std::u8string_view tracker_name;
			std::u8string_view tag_name;
		};

		// The rows of the report: every time line with the names which the manager resolves for it, sorted by the query which
		// collected it, the name of the tracker and the name of the tag, so the report of the same trace is always the same
		// instead of following the order in which the tags were sampled.
		std::pmr::vector<ReportRow> BuildReportRows(MemoryValueParser const& parser, std::pmr::memory_resource* resource)
		{
			auto const& manager = parser.GetMemoryTagsManager();
			auto time_lines = parser.GetTagTimeLines();

			std::pmr::vector<ReportRow> rows{ resource };
			rows.reserve(time_lines.size());

			for (auto const& time_line : time_lines)
			{
				ReportRow row;
				row.time_line = &time_line;

				if (auto tracker = manager.FindTracker(time_line.tracker_id); tracker != nullptr)
				{
					row.tracker_name = tracker->name;
				}

				if (auto tag = manager.FindTag(time_line.tag_id); tag != nullptr)
				{
					row.tag_name = tag->full_name;
				}

				rows.push_back(std::move(row));
			}

			std::sort(rows.begin(), rows.end(), [](ReportRow const& left, ReportRow const& right) {
				if (left.tracker_name != right.tracker_name)
				{
					return left.tracker_name < right.tracker_name;
				}

				if (left.tag_name != right.tag_name)
				{
					return left.tag_name < right.tag_name;
				}

				return left.time_line->tag_id.id < right.time_line->tag_id.id;
			});

			return rows;
		}

		std::wstring FormatName(std::u8string_view name)
		{
			std::wstring text;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(DisplayName(name), std::back_inserter(text));
			return text;
		}

		// The name of the roles of a point, which the report prints in front of the time of the point. A point which
		// carries more than one role carries them joined by '/', ex: First/Max.
		std::wstring PointKindName(MemoryValueParser::PointKind kind)
		{
			std::wstring text;
			auto append = [&text](std::wstring_view name) {
				if (!text.empty())
				{
					text += L'/';
				}
				text += name;
			};

			using enum MemoryValueParser::PointKind;
			if (MemoryValueParser::HasPointKind(kind, First))
			{
				append(L"First");
			}
			if (MemoryValueParser::HasPointKind(kind, Last))
			{
				append(L"Last");
			}
			if (MemoryValueParser::HasPointKind(kind, SegmentStart))
			{
				append(L"Start");
			}
			if (MemoryValueParser::HasPointKind(kind, SegmentEnd))
			{
				append(L"End");
			}
			if (MemoryValueParser::HasPointKind(kind, Max))
			{
				append(L"Max");
			}
			if (MemoryValueParser::HasPointKind(kind, Min))
			{
				append(L"Min");
			}
			if (MemoryValueParser::HasPointKind(kind, LastPeak))
			{
				append(L"LastPeak");
			}
			if (MemoryValueParser::HasPointKind(kind, Peak))
			{
				append(L"Peak");
			}
			if (MemoryValueParser::HasPointKind(kind, Sample))
			{
				append(L"Sample");
			}
			return text;
		}

		// The same name as UTF-8, which the column of the CSV file carries.
		std::u8string PointKindText(MemoryValueParser::PointKind kind)
		{
			std::u8string text;
			auto append = [&text](std::u8string_view name) {
				if (!text.empty())
				{
					text += u8'/';
				}
				text += name;
			};

			using enum MemoryValueParser::PointKind;
			if (MemoryValueParser::HasPointKind(kind, First))
			{
				append(u8"First");
			}
			if (MemoryValueParser::HasPointKind(kind, Last))
			{
				append(u8"Last");
			}
			if (MemoryValueParser::HasPointKind(kind, SegmentStart))
			{
				append(u8"Start");
			}
			if (MemoryValueParser::HasPointKind(kind, SegmentEnd))
			{
				append(u8"End");
			}
			if (MemoryValueParser::HasPointKind(kind, Max))
			{
				append(u8"Max");
			}
			if (MemoryValueParser::HasPointKind(kind, Min))
			{
				append(u8"Min");
			}
			if (MemoryValueParser::HasPointKind(kind, LastPeak))
			{
				append(u8"LastPeak");
			}
			if (MemoryValueParser::HasPointKind(kind, Peak))
			{
				append(u8"Peak");
			}
			if (MemoryValueParser::HasPointKind(kind, Sample))
			{
				append(u8"Sample");
			}
			return text;
		}

		std::wstring FormatSize(long double size_in_byte)
		{
			std::wstring text;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(FormatByteSize(size_in_byte), std::back_inserter(text));
			return text;
		}
	}

	MemoryValueParser::MemoryValueParser(Config in_config)
		: resource(in_config.resource)
		, memory_tags_manager(MemoryTagsManager::Config{ &resource })
		, tag_time_lines(&resource)
		, queries(&resource)
		, csv_path(std::move(in_config.csv_path))
		, mode(in_config.mode)
		, max_point_count(in_config.max_point_count)
		, min_slope_ratio(in_config.min_slope_ratio)
	{
		for (auto& query : in_config.queries)
		{
			queries.push_back(std::move(query));
		}
	}

	void MemoryValueParser::OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name)
	{
		memory_tags_manager.AddTag(tag_id, parent_id, tag_set_id, tag_name);
	}

	void MemoryValueParser::OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name)
	{
		memory_tags_manager.AddTracker(tracker_id, tracker_name);
	}

	void MemoryValueParser::OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name)
	{
		memory_tags_manager.AddTagSet(tag_set_id, tag_set_name);
	}

	void MemoryValueParser::OnMemoryTagSnap(MemoryTagSnapView snap_view)
	{
		if (queries.empty())
		{
			return;
		}

		DurationSec time = snap_view.GetTimePoint();
		MemoryTrackerId tracker_id = snap_view.GetTracker();

		// The name of the tracker is resolved once per snapshot, because the Thread condition of every query is matched
		// against it.
		std::u8string_view tracker_name = u8"Unknown";
		if (auto tracker = memory_tags_manager.FindTracker(tracker_id); tracker != nullptr && !tracker->name.empty())
		{
			tracker_name = tracker->name;
		}

		// Whether any query selects the tracker and the time point of the snapshot is decided once per snapshot, so the walk of
		// the tags only asks the question of the chain.
		is_snapshot_selected = false;
		for (auto const& query : queries)
		{
			if (IsMemoryTrackerMatched(query, tracker_name) && IsMemoryTimePointMatched(query, time))
			{
				is_snapshot_selected = true;
				break;
			}
		}

		if (!is_snapshot_selected)
		{
			return;
		}

		// Every tag of the snapshot is one time line, no matter how many queries select it: the name of the tag is the chain of
		// its parents, so the sample is added once as soon as any query matches the name.
		snap_view.ForeachMemoryTagValue([&](MemoryTagId tag_id, int64 value) {
			std::u8string_view tag_full_name;
			if (auto tag = memory_tags_manager.FindTag(tag_id); tag != nullptr)
			{
				tag_full_name = tag->full_name;
			}

			if (tag_full_name.empty())
			{
				return true;
			}

			bool is_tag_selected = false;
			for (auto const& query : queries)
			{
				if (IsMemoryTagNameMatched(query, tag_full_name))
				{
					is_tag_selected = true;
					break;
				}
			}

			if (is_tag_selected)
			{
				auto& time_line = FindTagTimeLine(tracker_id, tag_id);
				time_line.samples.push_back(ValueSample{ time, value });
			}

			return true;
		});
	}

	auto MemoryValueParser::FindTagTimeLine(MemoryTrackerId tracker_id, MemoryTagId tag_id) -> TagTimeLine&
	{
		auto found = std::find_if(tag_time_lines.begin(), tag_time_lines.end(), [tracker_id, tag_id](TagTimeLine const& time_line) {
			return time_line.tracker_id == tracker_id
				&& time_line.tag_id == tag_id;
		});

		if (found != tag_time_lines.end())
		{
			return *found;
		}

		auto& time_line = tag_time_lines.emplace_back(&resource);
		time_line.tracker_id = tracker_id;
		time_line.tag_id = tag_id;
		return time_line;
	}

	auto MemoryValueParser::PickPoints(std::span<ValueSample const> samples, std::size_t* extremum_count) -> std::pmr::vector<PeakPoint>
	{
		std::pmr::vector<PeakPoint> points(&resource);

		if (extremum_count != nullptr)
		{
			*extremum_count = 0;
		}

		if (samples.empty())
		{
			return points;
		}

		// The samples are ordered by the time, so the slopes are the ones of the time line and a tag which was streamed out of
		// the order is reported the same way as one which was not.
		std::pmr::vector<ValueSample> ordered(&resource);
		ordered.assign(samples.begin(), samples.end());

		std::sort(ordered.begin(), ordered.end(), [](ValueSample const& left, ValueSample const& right) {
			return left.time < right.time;
		});

		std::size_t sample_count = ordered.size();
		if (sample_count == 1)
		{
			PeakPoint point;
			point.time = ordered.front().time;
			point.value = ordered.front().value;
			point.slope = 0.0;
			point.kind = CombinePointKind(
				CombinePointKind(PointKind::First, PointKind::Last),
				CombinePointKind(PointKind::Max, PointKind::Min)
			);
			points.push_back(point);
			return points;
		}

		// The slope of every segment of the time line, in bytes per second, so slopes[index] is the slope from the sample at
		// index to the sample after it.
		std::pmr::vector<double> slopes(&resource);
		slopes.resize(sample_count - 1);

		for (std::size_t index = 0; index + 1 < sample_count; ++index)
		{
			double seconds = (ordered[index + 1].time - ordered[index].time).count();
			slopes[index] = seconds > 0.0
				? static_cast<double>(ordered[index + 1].value - ordered[index].value) / seconds
				: 0.0;
		}

		// The peaks are the samples where the sign of the slope changes; a zero slope is not a turning point.
		std::pmr::vector<std::size_t> selected(&resource);
		for (std::size_t index = 1; index + 1 < sample_count; ++index)
		{
			bool turning = (slopes[index - 1] > 0.0 && slopes[index] < 0.0)
				|| (slopes[index - 1] < 0.0 && slopes[index] > 0.0);
			if (turning)
			{
				selected.push_back(index);
			}
		}

		if (extremum_count != nullptr)
		{
			*extremum_count = selected.size();
		}

		// Drop the extremum points whose absolute slope is below the largest absolute slope of the tag multiplied by
		// the ratio, so a small turn of the value is not printed. A zero slope is not an extremum by the loop above, so
		// the largest absolute slope is never zero when the tag carries an extremum.
		double max_abs_slope = 0.0;
		for (std::size_t index : selected)
		{
			max_abs_slope = std::max(max_abs_slope, std::abs(slopes[index]));
		}

		double slope_ratio = std::clamp(min_slope_ratio, 0.0, 1.0);
		if (max_abs_slope > 0.0 && slope_ratio > 0.0)
		{
			double min_abs_slope = max_abs_slope * slope_ratio;
			std::pmr::vector<std::size_t> filtered(&resource);
			filtered.reserve(selected.size());

			for (std::size_t index : selected)
			{
				if (std::abs(slopes[index]) >= min_abs_slope)
				{
					filtered.push_back(index);
				}
			}

			selected = std::move(filtered);
		}

		// The number of the printed extremum points is limited by Config::max_point_count. When the tag keeps more
		// points than the limit, they are picked evenly, and the last one of the picked points is the LastPeak.
		if (max_point_count != 0 && selected.size() > max_point_count)
		{
			std::pmr::vector<std::size_t> picked(&resource);
			picked.reserve(max_point_count);

			for (std::size_t pick = 0; pick < max_point_count; ++pick)
			{
				std::size_t position = pick * selected.size() / max_point_count;
				std::size_t index = selected[position];
				if (picked.empty() || picked.back() != index)
				{
					picked.push_back(index);
				}
			}

			selected = std::move(picked);
		}

		// Find the first sample of the largest and of the smallest value, which are two of the four special points.
		std::size_t max_index = 0;
		std::size_t min_index = 0;
		for (std::size_t index = 1; index < sample_count; ++index)
		{
			if (ordered[index].value > ordered[max_index].value)
			{
				max_index = index;
			}
			if (ordered[index].value < ordered[min_index].value)
			{
				min_index = index;
			}
		}

		// Every sample carries the roles which it has, so a sample which is both the first and the largest carries
		// First/Max, and a sample which the mode selected carries the role of the peak as well.
		std::vector<PointKind> kinds(sample_count, static_cast<PointKind>(0));
		std::vector<std::uint8_t> has_point(sample_count, 0);

		auto add_point = [&kinds, &has_point](std::size_t index, PointKind kind) {
			has_point[index] = 1;
			kinds[index] = MemoryValueParser::CombinePointKind(kinds[index], kind);
		};

		add_point(0, PointKind::First);
		add_point(sample_count - 1, PointKind::Last);
		add_point(max_index, PointKind::Max);
		add_point(min_index, PointKind::Min);

		for (std::size_t index : selected)
		{
			add_point(index, PointKind::Peak);
		}

		// The last printed peak carries the role which tells that the sampling ends there.
		if (!selected.empty())
		{
			add_point(selected.back(), PointKind::LastPeak);
		}

		// The points are reported in the order of the time, which is the order of the sorted samples.
		for (std::size_t index = 0; index < sample_count; ++index)
		{
			if (has_point[index] == 0)
			{
				continue;
			}

			PeakPoint point;
			point.time = ordered[index].time;
			point.value = ordered[index].value;
			point.slope = index + 1 < sample_count ? slopes[index] : slopes.back();
			point.kind = kinds[index];
			points.push_back(point);
		}

		return points;
	}

	auto MemoryValueParser::BuildSegments(std::span<ValueSample const> samples) -> std::pmr::vector<SegmentReport>
	{
		std::pmr::vector<SegmentReport> segments(&resource);

		if (samples.empty())
		{
			return segments;
		}

		std::pmr::vector<ValueSample> ordered(&resource);
		ordered.assign(samples.begin(), samples.end());

		std::sort(ordered.begin(), ordered.end(), [](ValueSample const& left, ValueSample const& right) {
			return left.time < right.time;
		});

		std::size_t sample_count = ordered.size();
		DurationSec time_begin = ordered.front().time;
		DurationSec time_end = ordered.back().time;
		std::size_t segment_count = max_point_count == 0 ? 1 : max_point_count;
		if (sample_count == 1 || time_end <= time_begin)
		{
			segment_count = 1;
		}

		std::pmr::vector<double> slopes(&resource);
		if (sample_count > 1)
		{
			slopes.resize(sample_count - 1);
			for (std::size_t index = 0; index + 1 < sample_count; ++index)
			{
				double seconds = (ordered[index + 1].time - ordered[index].time).count();
				slopes[index] = seconds > 0.0
					? static_cast<double>(ordered[index + 1].value - ordered[index].value) / seconds
					: 0.0;
			}
		}

		DurationSec total_duration = time_end - time_begin;
		DurationSec segment_width{ total_duration.count() / static_cast<double>(segment_count) };

		std::size_t next_sample = 0;
		for (std::size_t segment_index = 0; segment_index < segment_count; ++segment_index)
		{
			DurationSec segment_begin = time_begin + segment_width * static_cast<double>(segment_index);
			DurationSec segment_end = segment_index + 1 == segment_count
				? time_end
				: time_begin + segment_width * static_cast<double>(segment_index + 1);

			std::size_t begin_index = next_sample;
			std::size_t end_index = begin_index;
			while (end_index < sample_count)
			{
				bool inside = segment_index + 1 == segment_count
					? ordered[end_index].time <= time_end
					: ordered[end_index].time < segment_end;
				if (!inside)
				{
					break;
				}
				++end_index;
			}
			next_sample = end_index;

			SegmentReport report{ &resource };
			report.index = segment_index;
			report.time_range = Potato::Misc::IndexSpan<DurationSec>{ segment_begin, segment_end };
			report.sample_count = end_index - begin_index;

			if (report.sample_count == 0)
			{
				segments.push_back(std::move(report));
				continue;
			}

			std::size_t max_index = begin_index;
			std::size_t min_index = begin_index;
			long double sum = 0.0L;

			for (std::size_t index = begin_index; index < end_index; ++index)
			{
				sum += static_cast<long double>(ordered[index].value);
				if (ordered[index].value > ordered[max_index].value)
				{
					max_index = index;
				}
				if (ordered[index].value < ordered[min_index].value)
				{
					min_index = index;
				}
			}

			report.mean = sum / static_cast<long double>(report.sample_count);

			std::vector<long double> values;
			values.reserve(report.sample_count);
			long double squared_deviation = 0.0L;

			for (std::size_t index = begin_index; index < end_index; ++index)
			{
				long double value = static_cast<long double>(ordered[index].value);
				values.push_back(value);
				long double difference = value - report.mean;
				squared_deviation += difference * difference;
			}

			std::sort(values.begin(), values.end());
			std::size_t middle = values.size() / 2;
			report.median = values.size() % 2 == 0
				? (values[middle - 1] + values[middle]) / 2.0L
				: values[middle];
			report.standard_deviation = std::sqrt(squared_deviation / static_cast<long double>(report.sample_count));

			std::size_t local_count = end_index - begin_index;
			std::vector<PointKind> kinds(local_count, static_cast<PointKind>(0));
			std::vector<std::uint8_t> has_point(local_count, 0);

			auto add_point = [&kinds, &has_point, begin_index](std::size_t index, PointKind kind) {
				std::size_t local = index - begin_index;
				has_point[local] = 1;
				kinds[local] = MemoryValueParser::CombinePointKind(kinds[local], kind);
			};

			add_point(begin_index, PointKind::SegmentStart);
			add_point(end_index - 1, PointKind::SegmentEnd);
			add_point(max_index, PointKind::Max);
			add_point(min_index, PointKind::Min);

			for (std::size_t local = 0; local < local_count; ++local)
			{
				if (has_point[local] == 0)
				{
					continue;
				}

				std::size_t index = begin_index + local;
				PeakPoint point;
				point.time = ordered[index].time;
				point.value = ordered[index].value;
				point.slope = sample_count > 1 && index + 1 < sample_count
					? slopes[index]
					: (sample_count > 1 ? slopes.back() : 0.0);
				point.kind = kinds[local];
				report.points.push_back(point);
			}

			segments.push_back(std::move(report));
		}

		return segments;
	}

	bool MemoryValueParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The report resolves the id of every tag and of every tracker to its name, and it reports the value which the
		// snapshots sampled for them, so the specs and the snapshots of the memory channel are both required, and no other
		// route of the trace is read.
		return flag == ParserRequireFlag::MEMORY_TAG
			|| flag == ParserRequireFlag::MEMORY_VALUE;
	}

	void MemoryValueParser::AllAnalyzeDone()
	{
		// The time lines are collected while the trace is read, so only the base parser is completed.
		Parser::AllAnalyzeDone();
	}

	bool MemoryValueParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		// The CSV file is written instead of the text report, which is what a caller which feeds the time lines into a chart
		// or a spreadsheet asks for.
		if (csv_path)
		{
			return WriteCSV();
		}

		auto print_line = [&printer](std::wstring_view text) {
			PrintReportLine(printer, text);
		};

		// The report can be long, so the count of the points which will be printed is counted first and told before the time
		// lines, and the reader is warned when the count is large.
		auto rows = BuildReportRows(*this, &resource);

		std::size_t total_sample_count = 0;
		std::size_t total_printed_count = 0;
		std::size_t total_extremum_count = 0;

		for (auto const& row : rows)
		{
			total_sample_count += row.time_line->samples.size();
			if (mode == PointMode::Peak)
			{
				std::size_t extremum_count = 0;
				total_printed_count += PickPoints(row.time_line->samples, &extremum_count).size();
				total_extremum_count += extremum_count;
			}
			else {
				for (auto const& segment : BuildSegments(row.time_line->samples))
				{
					total_printed_count += segment.points.size();
				}
			}
		}

		print_line(L"MemoryValueParser Output:");
		print_line(std::format(L"    Memory Tag <{}>, Memory Tracker <{}>, Memory Tag Set <{}>, Memory Value Line <{}>", memory_tags_manager.GetTagCount(), memory_tags_manager.GetTrackerCount(), memory_tags_manager.GetTagSetCount(), tag_time_lines.size()));

		if (mode == PointMode::Peak)
		{
			print_line(std::format(
				L"    Mode: <Peak>, Extremum Points: <{}>, Filter Ratio: <{:.4f}>, Point <{}> of <{}>",
				total_extremum_count,
				std::clamp(min_slope_ratio, 0.0, 1.0),
				total_printed_count,
				total_sample_count
			));
		}
		else {
			std::size_t segment_count = max_point_count == 0 ? 1 : max_point_count;
			print_line(std::format(
				L"    Mode: <All>, Segments: <{}>, Point <{}> of <{}>",
				segment_count,
				total_printed_count,
				total_sample_count
			));
		}

		// The report is large, and a caller which did not expect it is better warned than left with a slow terminal: the
		// warning is a part of the report, so a report which is written into a file carries it as well.
		if (total_printed_count > 1000)
		{
			if (mode == PointMode::Peak)
			{
				print_line(std::format(L"    Warning: <{}> point(s) printed; lower -mvp or raise -mvs.", total_printed_count));
			}
			else {
				print_line(std::format(L"    Warning: <{}> point(s) printed; lower -mvp or use CSV.", total_printed_count));
			}
		}

		if (tag_time_lines.empty())
		{
			print_line(L"    No memory tag is selected by the query, or the trace carries no memory tag snapshot.");
			return true;
		}

		std::u8string_view current_tracker_name;

		for (auto const& row : rows)
		{
			auto const& time_line = *row.time_line;

			// The rows are ordered by the tracker and then by the tag, so a tag of the next tracker starts its own block.
			if (current_tracker_name != row.tracker_name)
			{
				current_tracker_name = row.tracker_name;
				print_line(std::format(L"\nTracker:<{}>:", FormatName(current_tracker_name)));
			}

			if (mode == PointMode::Peak)
			{
				std::size_t extremum_count = 0;
				auto picked = PickPoints(time_line.samples, &extremum_count);

				print_line(std::format(
					L"        Tracker:<{}>, MemoryTag:<{}>, Extremum:<{}>, Point <{}> of <{}>",
					FormatName(row.tracker_name),
					FormatName(row.tag_name),
					extremum_count,
					picked.size(),
					time_line.samples.size()
				));

				for (auto const& point : picked)
				{
					// Every point carries the name of its place in the time line, the time, the value and the slope of the values,
					// which is a size in bytes per second.
					print_line(std::format(
						L"            {:<12}: Time:<{}>, Value:<{}>, Slope:<{}>",
						PointKindName(point.kind),
						FormatTimePoint(point.time),
						FormatSize(static_cast<long double>(point.value)),
						FormatSlope(point.slope)
					));
				}
			}
			else {
				auto segments = BuildSegments(time_line.samples);

				std::size_t point_count = 0;
				for (auto const& segment : segments)
				{
					point_count += segment.points.size();
				}

				print_line(std::format(
					L"        Tracker:<{}>, MemoryTag:<{}>, Point <{}> of <{}>",
					FormatName(row.tracker_name),
					FormatName(row.tag_name),
					point_count,
					time_line.samples.size()
				));

				std::size_t segment_count = segments.size();
				for (auto const& segment : segments)
				{
					if (segment.sample_count == 0)
					{
						print_line(std::format(
							L"            Segment <{}/{}> [{}, {}]: No sample",
							segment.index + 1,
							segment_count,
							FormatTimePoint(segment.time_range.Begin()),
							FormatTimePoint(segment.time_range.End())
						));
						continue;
					}

					print_line(std::format(
						L"            Segment <{}/{}> [{}, {}], Samples <{}>:",
						segment.index + 1,
						segment_count,
						FormatTimePoint(segment.time_range.Begin()),
						FormatTimePoint(segment.time_range.End()),
						segment.sample_count
					));

					for (auto const& point : segment.points)
					{
						print_line(std::format(
							L"                {:<12}: Time:<{}>, Value:<{}>, Slope:<{}>",
							PointKindName(point.kind),
							FormatTimePoint(point.time),
							FormatSize(static_cast<long double>(point.value)),
							FormatSlope(point.slope)
						));
					}

					print_line(std::format(
						L"                Mean:<{}>, Median:<{}>, StdDev:<{}>",
						FormatSize(segment.mean),
						FormatSize(segment.median),
						FormatSize(segment.standard_deviation)
					));
				}
			}
		}

		return true;
	}

	bool MemoryValueParser::WriteCSV()
	{
		if (!csv_path)
		{
			return false;
		}

		// The file is created and its previous content is emptied, the same as the report of -o, so the file always carries
		// the time lines of one analysis.
		Potato::Document::DocumentWriter writer{
			*csv_path,
			Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY
		};

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the CSV file <{}>">(csv_path->generic_wstring());
			return false;
		}

		std::u8string content;
		CSVRowWriter row{ content };

		// The header names every column, and the name of the value column carries the unit of the value, which is the byte.
		row.Text(u8"Tracker");
		row.Text(u8"MemoryTag");
		row.Text(u8"PointIndex");
		row.Text(u8"Kind");
		row.Text(u8"Time_s");
		row.Text(u8"Value_B");
		row.Text(u8"Slope_Bps");
		row.EndLine();

		for (auto const& report_row : BuildReportRows(*this, &resource))
		{
			auto const& time_line = *report_row.time_line;

			std::pmr::vector<PeakPoint> picked(&resource);
			if (mode == PointMode::Peak)
			{
				picked = PickPoints(time_line.samples);
			}
			else {
				for (auto const& segment : BuildSegments(time_line.samples))
				{
					picked.insert(picked.end(), segment.points.begin(), segment.points.end());
				}
			}

			for (std::size_t point_index = 0; point_index < picked.size(); ++point_index)
			{
				row.Text(report_row.tracker_name);
				row.Text(report_row.tag_name);
				row.Integer(point_index + 1);
				row.Text(PointKindText(picked[point_index].kind));
				row.Seconds(picked[point_index].time);
				row.Bytes(picked[point_index].value);
				row.Decimal(static_cast<long double>(picked[point_index].slope));
				row.EndLine();
			}
		}

		writer.Write(std::span<std::byte const>{
			reinterpret_cast<std::byte const*>(content.data()),
			content.size()
		});

		return true;
	}
}
