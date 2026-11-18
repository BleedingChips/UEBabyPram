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

		// The name of the place of a point in the time line, which the report prints in front of the time of the point.
		std::wstring_view PointKindName(MemoryValueParser::PointKind kind)
		{
			switch (kind)
			{
			case MemoryValueParser::PointKind::First:
				return L"First";
			case MemoryValueParser::PointKind::Last:
				return L"Last";
			case MemoryValueParser::PointKind::Sample:
				return L"Sample";
			case MemoryValueParser::PointKind::Peak:
			default:
				return L"Peak";
			}
		}

		// The same name as UTF-8, which the column of the CSV file carries.
		std::u8string_view PointKindText(MemoryValueParser::PointKind kind)
		{
			switch (kind)
			{
			case MemoryValueParser::PointKind::First:
				return u8"First";
			case MemoryValueParser::PointKind::Last:
				return u8"Last";
			case MemoryValueParser::PointKind::Sample:
				return u8"Sample";
			case MemoryValueParser::PointKind::Peak:
			default:
				return u8"Peak";
			}
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
		, max_peak_count(in_config.max_peak_count)
		, min_peak_sample_count(in_config.min_peak_sample_count)
		, all_samples(in_config.all_samples)
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

	auto MemoryValueParser::PickPeaks(std::span<ValueSample const> samples) -> std::pmr::vector<PeakPoint>
	{
		std::pmr::vector<PeakPoint> points(&resource);

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

		// A time line of one sample has no slope at all, and its only sample is both the first and the last point of it.
		if (ordered.size() == 1)
		{
			PeakPoint point;
			point.time = ordered.front().time;
			point.value = ordered.front().value;
			point.slope = 0.0;
			point.kind = PointKind::First;
			points.push_back(point);
			return points;
		}

		// The slope of every segment of the time line, in bytes per second, so slopes[index] is the slope from the sample at
		// index to the sample after it.
		std::pmr::vector<double> slopes(&resource);
		slopes.resize(ordered.size() - 1);

		for (std::size_t index = 0; index + 1 < ordered.size(); ++index)
		{
			double seconds = (ordered[index + 1].time - ordered[index].time).count();
			slopes[index] = seconds > 0.0
				? static_cast<double>(ordered[index + 1].value - ordered[index].value) / seconds
				: 0.0;
		}

		// The mode of the whole time line prints every sample of the tag, and the limits of the peaks do not apply to it: every
		// sample is a point, with the slope which leaves it, and the last sample carries the slope which enters it.
		if (all_samples)
		{
			for (std::size_t index = 0; index < ordered.size(); ++index)
			{
				PeakPoint point;
				point.time = ordered[index].time;
				point.value = ordered[index].value;
				point.kind = index == 0
					? PointKind::First
					: (index + 1 == ordered.size() ? PointKind::Last : PointKind::Sample);
				point.slope = index + 1 < ordered.size() ? slopes[index] : slopes.back();
				points.push_back(point);
			}

			return points;
		}

		auto slope_sign = [](double slope) {
			return slope > 0.0 ? 1 : (slope < 0.0 ? -1 : 0);
		};

		// The first point is always printed, with the slope which leaves it.
		PeakPoint first_point;
		first_point.time = ordered.front().time;
		first_point.value = ordered.front().value;
		first_point.slope = slopes.front();
		first_point.kind = PointKind::First;
		points.push_back(first_point);

		// The peaks are the samples where the slope turns: a slope which goes from positive to negative is a peak, one which
		// goes from negative to positive is a valley, and a slope which becomes zero is a plateau, which is a peak as well
		// because the values stopped moving there.
		std::pmr::vector<std::size_t> peak_indices(&resource);

		for (std::size_t index = 1; index + 1 < ordered.size(); ++index)
		{
			if (slope_sign(slopes[index - 1]) == slope_sign(slopes[index]))
			{
				continue;
			}

			// The peaks of one tag may be thousands, so two printed peaks are at least the configured count of samples apart,
			// which drops the peaks of a tag which oscillates around one value.
			if (min_peak_sample_count != 0 && !peak_indices.empty() && index - peak_indices.back() < min_peak_sample_count)
			{
				continue;
			}

			// A peak which is less than 5% away from the peak before it, measured against the larger of the two values, is a
			// small wave which the shape of the tag does not need: it is dropped unless it is the first or the last point,
			// which are kept by the loops of the ends.
			int64 previous_value = peak_indices.empty() ? first_point.value : ordered[peak_indices.back()].value;
			int64 current_value = ordered[index].value;
			int64 difference = current_value > previous_value ? current_value - previous_value : previous_value - current_value;
			int64 larger = current_value > previous_value ? current_value : previous_value;
			larger = larger < 0 ? -larger : larger;

			if (larger != 0 && static_cast<long double>(difference) < static_cast<long double>(larger) * 0.05L)
			{
				continue;
			}

			peak_indices.push_back(index);
		}

		// The count of the printed peaks is limited: when the tag carries more peaks than the limit, the peaks are picked
		// evenly from the ones which survived, so the report still covers the whole time line instead of its beginning.
		if (max_peak_count != 0 && peak_indices.size() > max_peak_count)
		{
			std::pmr::vector<std::size_t> picked(&resource);
			picked.reserve(max_peak_count);

			for (std::size_t pick = 0; pick < max_peak_count; ++pick)
			{
				std::size_t index = pick * peak_indices.size() / max_peak_count;
				if (picked.empty() || picked.back() != index)
				{
					picked.push_back(index);
				}
			}

			peak_indices = std::move(picked);
		}

		for (std::size_t index : peak_indices)
		{
			PeakPoint point;
			point.time = ordered[index].time;
			point.value = ordered[index].value;
			// The slope of a peak is the one which leaves it, which is the direction which the values took after the turn.
			point.slope = slopes[index];
			point.kind = PointKind::Peak;
			points.push_back(point);
		}

		// The last point is always printed, with the slope which enters it.
		PeakPoint last_point;
		last_point.time = ordered.back().time;
		last_point.value = ordered.back().value;
		last_point.slope = slopes.back();
		last_point.kind = PointKind::Last;
		points.push_back(last_point);

		return points;
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

		for (auto const& row : rows)
		{
			total_sample_count += row.time_line->samples.size();
			total_printed_count += PickPeaks(row.time_line->samples).size();
		}

		print_line(L"MemoryValueParser Output:");
		print_line(L"    (Memory Value: the time and the value of every snapshot which carries a memory tag which the query selects, in the order of the time)");
		if (all_samples)
		{
			print_line(L"    (Every sample of every tag is printed, with the slope of the values at it, which is a size in bytes per second)");
		}
		else {
			print_line(L"    (The report prints the first sample of a tag, the last one, and the peaks between them, which are the samples where the slope of the values turns; a peak which is less than 5% away from the peak before it is a small wave and is dropped)");
		}
		print_line(std::format(L"    Memory Tag <{}>, Memory Tracker <{}>, Memory Tag Set <{}>, Memory Value Line <{}>", memory_tags_manager.GetTagCount(), memory_tags_manager.GetTrackerCount(), memory_tags_manager.GetTagSetCount(), tag_time_lines.size()));
		if (all_samples)
		{
			print_line(std::format(L"    All samples, Point <{}> of <{}>", total_printed_count, total_sample_count));
		}
		else {
			print_line(std::format(
				L"    Limit of the peaks of one tag: <{}>, Sample between the peaks: <{}>, Point <{}> of <{}>",
				max_peak_count == 0 ? L"unlimited" : std::format(L"{}", max_peak_count),
				min_peak_sample_count,
				total_printed_count,
				total_sample_count
			));
		}

		// The report is large, and a caller which did not expect it is better warned than left with a slow terminal: the
		// warning is a part of the report, so a report which is written into a file carries it as well.
		if (total_printed_count > 1000)
		{
			if (all_samples)
			{
				print_line(std::format(L"    Warning: <{}> point(s) will be printed, which is a large report. Write the report into a CSV file, or drop --all_samples to print only the peaks.", total_printed_count));
			}
			else {
				print_line(std::format(L"    Warning: <{}> point(s) will be printed, which is a large report. Lower the limit of the peaks of one tag, or write the report into a CSV file.", total_printed_count));
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

			auto picked = PickPeaks(time_line.samples);

			print_line(std::format(
				L"        Tracker:<{}>, MemoryTag:<{}>, Point <{}> of <{}>",
				FormatName(row.tracker_name),
				FormatName(row.tag_name),
				picked.size(),
				time_line.samples.size()
			));

			for (auto const& point : picked)
			{
				// Every point carries the name of its place in the time line, the time, the value and the slope of the values,
				// which is a size in bytes per second.
				print_line(std::format(
					L"            {:<5}: [{}]=<{}>, Slope:<{}>",
					PointKindName(point.kind),
					FormatTimePoint(point.time),
					FormatSize(static_cast<long double>(point.value)),
					FormatSlope(point.slope)
				));
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
			auto picked = PickPeaks(time_line.samples);

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
