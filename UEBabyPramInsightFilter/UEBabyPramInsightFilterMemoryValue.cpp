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
				if (left.time_line->query_index != right.time_line->query_index)
				{
					return left.time_line->query_index < right.time_line->query_index;
				}

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
		, max_value_count(in_config.max_value_count)
		, snapshot_query_indices(&resource)
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

		// The queries which select the tracker and the time point of the snapshot are decided once per snapshot, so the walk
		// of the tags only asks the question of the chain for them.
		snapshot_query_indices.clear();
		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto const& query = queries[query_index];
			if (IsMemoryTrackerMatched(query, tracker_name) && IsMemoryTimePointMatched(query, time))
			{
				snapshot_query_indices.push_back(query_index);
			}
		}

		if (snapshot_query_indices.empty())
		{
			return;
		}

		// Every tag of the snapshot is one time line per query which selects it: the name of the tag is the chain of its
		// parents, so the chain of the query is matched against the segments of the name.
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

			for (std::size_t query_index : snapshot_query_indices)
			{
				if (!IsMemoryTagNameMatched(queries[query_index], tag_full_name))
				{
					continue;
				}

				auto& time_line = FindTagTimeLine(query_index, tracker_id, tag_id);
				time_line.samples.push_back(ValueSample{ time, value });
			}

			return true;
		});
	}

	auto MemoryValueParser::FindTagTimeLine(std::size_t query_index, MemoryTrackerId tracker_id, MemoryTagId tag_id) -> TagTimeLine&
	{
		auto found = std::find_if(tag_time_lines.begin(), tag_time_lines.end(), [query_index, tracker_id, tag_id](TagTimeLine const& time_line) {
			return time_line.query_index == query_index
				&& time_line.tracker_id == tracker_id
				&& time_line.tag_id == tag_id;
		});

		if (found != tag_time_lines.end())
		{
			return *found;
		}

		auto& time_line = tag_time_lines.emplace_back(&resource);
		time_line.query_index = query_index;
		time_line.tracker_id = tracker_id;
		time_line.tag_id = tag_id;
		return time_line;
	}

	auto MemoryValueParser::PickSamples(std::span<ValueSample const> samples) -> std::pmr::vector<ValueSample>
	{
		std::pmr::vector<ValueSample> picked(&resource);

		if (samples.empty())
		{
			return picked;
		}

		// Every sample is printed when the count is 0 or the time line is already short enough, and the samples are kept in
		// the order of the time which they were collected in.
		if (max_value_count == 0 || samples.size() <= max_value_count)
		{
			picked.assign(samples.begin(), samples.end());
			return picked;
		}

		// The samples are picked by an even time interval from the first sample to the last one: the point of the interval
		// is the time which the report would carry if the time line were sampled evenly, and the sample which is nearest to
		// that point is the one which is printed. The first and the last sample are always printed, so the report always
		// carries the ends of the time line.
		DurationSec first_time = samples.front().time;
		DurationSec last_time = samples.front().time;

		for (auto const& sample : samples)
		{
			first_time = std::min(first_time, sample.time);
			last_time = std::max(last_time, sample.time);
		}

		// The samples are ordered by the time first, so the sample which is nearest to a point of the interval is found by a
		// walk which only moves forward, and a time line which was streamed out of the order is sorted here instead of being
		// picked by a walk which would miss it.
		std::pmr::vector<ValueSample> ordered(&resource);
		ordered.assign(samples.begin(), samples.end());
		std::sort(ordered.begin(), ordered.end(), [](ValueSample const& left, ValueSample const& right) {
			return left.time < right.time;
		});

		double span = (last_time - first_time).count();

		for (std::size_t point_index = 0; point_index < max_value_count; ++point_index)
		{
			// The points of the interval are its beginning, its end and the points which divide it into the same parts, so
			// the first point is the first sample and the last point is the last sample.
			double ratio = max_value_count <= 1
				? 0.0
				: static_cast<double>(point_index) / static_cast<double>(max_value_count - 1);

			DurationSec target = first_time + DurationSec{ span * ratio };

			// The sample which is nearest to the point: the walk of the ordered samples stops at the first one which is not
			// before the point, and the nearer of it and of the one before it is the one which is printed.
			auto after = std::lower_bound(ordered.begin(), ordered.end(), target, [](ValueSample const& sample, DurationSec time) {
				return sample.time < time;
			});

			std::size_t nearest_index = 0;
			if (after == ordered.end())
			{
				nearest_index = ordered.size() - 1;
			}
			else if (after == ordered.begin())
			{
				nearest_index = 0;
			}
			else {
				std::size_t next_index = static_cast<std::size_t>(after - ordered.begin());
				std::size_t previous_index = next_index - 1;

				DurationSec next_distance = ordered[next_index].time - target;
				DurationSec previous_distance = target - ordered[previous_index].time;
				nearest_index = previous_distance <= next_distance ? previous_index : next_index;
			}

			// The same sample may be the nearest one to more than one point of a short time line, so it is only kept once.
			if (picked.empty() || picked.back().time != ordered[nearest_index].time)
			{
				picked.push_back(ordered[nearest_index]);
			}
		}

		return picked;
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

		// The report can be very long, so the count of the samples which will be printed is counted first and told before
		// the time lines, and the reader is warned when the count is large.
		auto rows = BuildReportRows(*this, &resource);

		std::size_t total_sample_count = 0;
		std::size_t total_printed_count = 0;

		for (auto const& row : rows)
		{
			total_sample_count += row.time_line->samples.size();
			total_printed_count += PickSamples(row.time_line->samples).size();
		}

		print_line(L"MemoryValueParser Output:");
		print_line(L"    (Memory Value: the time and the value of every snapshot which carries a memory tag which the query selects, in the order of the time)");
		print_line(L"    (The samples of a tag are picked by an even time interval when the tag carries more of them than the limit, and the sample which is nearest to every point of the interval is printed)");
		print_line(std::format(L"    Memory Tag <{}>, Memory Tracker <{}>, Memory Tag Set <{}>, Memory Value Line <{}>", memory_tags_manager.GetTagCount(), memory_tags_manager.GetTrackerCount(), memory_tags_manager.GetTagSetCount(), tag_time_lines.size()));
		print_line(std::format(L"    Limit of the samples of one tag: <{}>, Sample <{}> of <{}>", max_value_count == 0 ? L"unlimited" : std::format(L"{}", max_value_count), total_printed_count, total_sample_count));

		// The report is large, and a caller which did not expect it is better warned than left with a slow terminal: the
		// warning is a part of the report, so a report which is written into a file carries it as well.
		if (total_printed_count > 1000)
		{
			print_line(std::format(L"    Warning: <{}> sample(s) will be printed, which is a large report. Lower the limit of the samples of one tag, or write the report into a CSV file.", total_printed_count));
		}

		if (tag_time_lines.empty())
		{
			print_line(L"    No memory tag is selected by the query, or the trace carries no memory tag snapshot.");
			return true;
		}

		std::size_t current_query_index = std::numeric_limits<std::size_t>::max();

		for (auto const& row : rows)
		{
			auto const& time_line = *row.time_line;

			if (time_line.query_index != current_query_index)
			{
				current_query_index = time_line.query_index;
				print_line(std::format(L"\nQuery <{}>:", current_query_index));
			}

			auto picked = PickSamples(time_line.samples);

			print_line(std::format(
				L"        Tracker:<{}>, MemoryTag:<{}>, Sample <{}> of <{}>",
				FormatName(row.tracker_name),
				FormatName(row.tag_name),
				picked.size(),
				time_line.samples.size()
			));

			for (auto const& sample : picked)
			{
				print_line(std::format(
					L"            [{}]=<{}>",
					DisplayTimePoint{ sample.time },
					FormatSize(static_cast<long double>(sample.value))
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
		row.Text(u8"QueryIndex");
		row.Text(u8"Tracker");
		row.Text(u8"MemoryTag");
		row.Text(u8"SampleIndex");
		row.Text(u8"Time_s");
		row.Text(u8"Value_B");
		row.EndLine();

		for (auto const& report_row : BuildReportRows(*this, &resource))
		{
			auto const& time_line = *report_row.time_line;
			auto picked = PickSamples(time_line.samples);

			for (std::size_t sample_index = 0; sample_index < picked.size(); ++sample_index)
			{
				row.Integer(time_line.query_index);
				row.Text(report_row.tracker_name);
				row.Text(report_row.tag_name);
				row.Integer(sample_index + 1);
				row.Seconds(picked[sample_index].time);
				row.Bytes(picked[sample_index].value);
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
