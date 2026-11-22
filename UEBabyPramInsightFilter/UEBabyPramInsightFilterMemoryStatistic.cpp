module;

// The C header is included instead of <cassert>, which would pull the C++ headers of the implementation into the global
// module fragment and make their specializations of std::formatter ambiguous with the ones which the std module carries.
#include <assert.h>

module UEBabyPramInsightFilterMemoryStatistic;

import UEBabyPramInsightFilterParser;
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
		// One row of the report: the statistics of a tag with the names which the manager resolves for it, so the sort of the
		// report compares the names instead of looking them up again.
		struct ReportRow
		{
			MemoryStatisticParser::TagStatistic const* statistic = nullptr;
			std::u8string_view tracker_name;
			std::u8string_view tag_name;
		};

		// The name of a spec which the trace declares with an empty name is reported as unknown instead of an empty pair of
		// angle brackets.
		std::u8string_view DisplayName(std::u8string_view name)
		{
			return name.empty()
				? std::u8string_view{ u8"<Unknown>" }
				: name;
		}
		// The square root of a value, by the Newton method, instead of std::sqrt: the C++ header which carries std::sqrt
		// cannot be included in the global module fragment of this translation unit, because its specializations of
		// std::formatter would be ambiguous with the ones which the std module carries.
		double SqrtValue(double value)
		{
			if (!(value > 0.0))
			{
				return 0.0;
			}

			double estimate = value > 1.0 ? value : 1.0;

			for (int iteration = 0; iteration < 32; ++iteration)
			{
				double next = 0.5 * (estimate + value / estimate);
				if (next == estimate)
				{
					break;
				}
				estimate = next;
			}

			return estimate;
		}


		// The name of the order of the report, which the report carries so the reader knows why the rows are where they are.
		std::wstring_view SortKindName(MemoryStatisticParser::SortKind sort)
		{
			switch (sort)
			{
			case MemoryStatisticParser::SortKind::Mean:
				return L"mean";
			case MemoryStatisticParser::SortKind::Max:
				return L"max";
			case MemoryStatisticParser::SortKind::Name:
			default:
				return L"name";
			}
		}


		// Accumulate one snapshot of the tag into the statistics: the first and the last snapshot are kept with the value
		// which they carried, the largest and the smallest value are kept with the snapshot which carried them, and the sum
		// of the values is kept for the mean.
		void AccumulateStatistic(MemoryStatisticParser::TagStatistic& statistic, DurationSec time, int64 value)
		{
			if (statistic.count == 0)
			{
				statistic.first_time = time;
				statistic.first_value = value;
				statistic.last_time = time;
				statistic.last_value = value;
				statistic.max_time = time;
				statistic.max_value = value;
				statistic.min_time = time;
				statistic.min_value = value;
			}
			else {
				// The snapshots of a tracker are streamed in the order of their time, but the first and the last snapshot
				// are decided by their time instead of their position, so a trace which streams them out of the order still
				// reports the ends of the range.
				if (time < statistic.first_time)
				{
					statistic.first_time = time;
					statistic.first_value = value;
				}

				if (time > statistic.last_time)
				{
					statistic.last_time = time;
					statistic.last_value = value;
				}

				if (value > statistic.max_value)
				{
					statistic.max_time = time;
					statistic.max_value = value;
				}

				if (value < statistic.min_value)
				{
					statistic.min_time = time;
					statistic.min_value = value;
				}
			}

			// The samples are kept as well, because the trend of the statistic is computed from the samples between the
			// first snapshot of the largest value and the first snapshot of the smallest one, which is only known at the end
			// of the trace.
			statistic.samples.push_back(MemoryStatisticParser::ValueSample{ time, value });

			statistic.count += 1;
			statistic.sum += static_cast<long double>(value);
		}

		// The rows of the report of the statistics: every statistic with the names which the manager resolves for it, sorted
		// by the order which the caller asks for. The name order is the order of the data, the query which accumulated the
		// statistic, the name of the tracker and the name of the tag, so the report of the same trace is always the same; the
		// other orders put the largest value first and fall back to the name order for the statistics which carry the same
		// value, so the report stays deterministic.
		std::pmr::vector<ReportRow> BuildReportRows(MemoryStatisticParser const& parser, MemoryStatisticParser::SortKind sort, std::pmr::memory_resource* resource)
		{
			auto const& manager = parser.GetMemoryTagsManager();
			auto statistics = parser.GetTagStatistics();

			std::pmr::vector<ReportRow> rows{ resource };
			rows.reserve(statistics.size());

			for (auto const& statistic : statistics)
			{
				ReportRow row;
				row.statistic = &statistic;

				if (auto tracker = manager.FindTracker(statistic.tracker_id); tracker != nullptr)
				{
					row.tracker_name = tracker->name;
				}

				if (auto tag = manager.FindTag(statistic.tag_id); tag != nullptr)
				{
					row.tag_name = tag->full_name;
				}

				rows.push_back(std::move(row));
			}

			// The order by name is the fallback of every order, so two statistics which carry the same value are still
			// ordered by the tracker and the tag, and the report of the same trace never depends on the sort of the storage.
			auto is_name_less = [](ReportRow const& left, ReportRow const& right) {
				if (left.tracker_name != right.tracker_name)
				{
					return left.tracker_name < right.tracker_name;
				}

				if (left.tag_name != right.tag_name)
				{
					return left.tag_name < right.tag_name;
				}

				return left.statistic->tag_id.id < right.statistic->tag_id.id;
			};

			std::sort(rows.begin(), rows.end(), [sort, is_name_less](ReportRow const& left, ReportRow const& right) {
				switch (sort)
				{
				case MemoryStatisticParser::SortKind::Mean:
				{
					long double left_mean = left.statistic->sum / static_cast<long double>(left.statistic->count);
					long double right_mean = right.statistic->sum / static_cast<long double>(right.statistic->count);

					// The largest mean first, and the name order decides between two statistics of the same mean.
					if (left_mean != right_mean)
					{
						return left_mean > right_mean;
					}

					return is_name_less(left, right);
				}
				case MemoryStatisticParser::SortKind::Max:
				{
					if (left.statistic->max_value != right.statistic->max_value)
					{
						return left.statistic->max_value > right.statistic->max_value;
					}

					return is_name_less(left, right);
				}
				case MemoryStatisticParser::SortKind::Name:
				default:
					return is_name_less(left, right);
				}
			});

			return rows;
		}
	}

	MemoryStatisticParser::MemoryStatisticParser(Config in_config)
		: resource(in_config.resource)
		, memory_tags_manager(MemoryTagsManager::Config{ &resource })
		, tag_statistics(&resource)
		, queries(&resource)
		, csv_path(std::move(in_config.csv_path))
		, sort(in_config.sort)
		, max_tag_count(in_config.max_tag_count)
	{
		for (auto& query : in_config.queries)
		{
			queries.push_back(std::move(query));
		}
	}

	void MemoryStatisticParser::OnMemoryTagSpecId(MemoryTagId tag_id, MemoryTagId parent_id, MemoryTagSetId tag_set_id, std::u8string_view tag_name)
	{
		memory_tags_manager.AddTag(tag_id, parent_id, tag_set_id, tag_name);
	}

	void MemoryStatisticParser::OnMemoryTrackerSpec(MemoryTrackerId tracker_id, std::u8string_view tracker_name)
	{
		memory_tags_manager.AddTracker(tracker_id, tracker_name);
	}

	void MemoryStatisticParser::OnMemoryTagSetSpec(MemoryTagSetId tag_set_id, std::u8string_view tag_set_name)
	{
		memory_tags_manager.AddTagSet(tag_set_id, tag_set_name);
	}

	void MemoryStatisticParser::OnMemoryTagSnap(MemoryTagSnapView snap_view)
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

		// Whether any query selects the tracker and the time point of the snapshot is decided once per snapshot, so the walk
		// of the tags only asks the question of the chain.
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

		// Every tag of the snapshot is one unit, no matter how many queries select it: the name of the tag is the chain of its
		// parents, so a tag is accumulated once as soon as any query matches its name.
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
				AccumulateStatistic(FindTagStatistic(tracker_id, tag_id), time, value);
			}

			return true;
		});
	}

	auto MemoryStatisticParser::FindTagStatistic(MemoryTrackerId tracker_id, MemoryTagId tag_id) -> TagStatistic&
	{
		auto found = std::find_if(tag_statistics.begin(), tag_statistics.end(), [tracker_id, tag_id](TagStatistic const& statistic) {
			return statistic.tracker_id == tracker_id
				&& statistic.tag_id == tag_id;
		});

		if (found != tag_statistics.end())
		{
			return *found;
		}

		// The statistics of a tag which is sampled for the first time are created here, so the count of the entries is the
		// count of the tags which the queries select. The entry owns the samples of the tag, so it is created with the resource
		// of the parser.
		auto& statistic = tag_statistics.emplace_back(&resource);
		statistic.tracker_id = tracker_id;
		statistic.tag_id = tag_id;
		return statistic;
	}

	bool MemoryStatisticParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The report resolves the id of every tag and of every tracker to its name, and it reports the value which the
		// snapshots sampled for them, so the specs and the snapshots of the memory channel are both required, and no other
		// route of the trace is read.
		return flag == ParserRequireFlag::MEMORY_TAG
			|| flag == ParserRequireFlag::MEMORY_VALUE;
	}

	void MemoryStatisticParser::AllAnalyzeDone()
	{
		// The statistics are accumulated while the trace is read, so only the base parser is completed.
		Parser::AllAnalyzeDone();
	}

	bool MemoryStatisticParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		// The CSV file is written instead of the text report, which is what a caller which feeds the statistics into a
		// spreadsheet asks for.
		if (csv_path)
		{
			return WriteCSV();
		}

		// The lines of the report are formatted here and handed to the printer, which is the helper of the report lines:
		// the pattern formatter of Potato::Log::LogTo resolves the string wrapper of a message ambiguously in this
		// translation unit, which is a build error, and a line of this report needs no pattern of its own.
		auto print_line = [&printer](std::wstring_view text) {
			PrintReportLine(printer, text);
		};

		auto format_size = [](long double size_in_byte) {
			std::wstring text;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(FormatByteSize(size_in_byte), std::back_inserter(text));
			return text;
		};

		auto format_name = [](std::u8string_view name) {
			std::wstring text;
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(DisplayName(name), std::back_inserter(text));
			return text;
		};

		print_line(L"MemoryStatisticParser Output:");

		print_line(std::format(L"    Sorted by: <{}>", SortKindName(sort)));

		print_line(std::format(
			L"    Memory Tag <{}>, Memory Tracker <{}>, Memory Tag Set <{}>, Memory Tag Statistic <{}>",
			memory_tags_manager.GetTagCount(),
			memory_tags_manager.GetTrackerCount(),
			memory_tags_manager.GetTagSetCount(),
			tag_statistics.size()
		));

		if (tag_statistics.empty())
		{
			print_line(L"    No memory tag is selected by the query, or the trace carries no memory tag snapshot.");
			return true;
		}

		// The statistics are reported by the name of the tracker and then by the name of the tag, so the report of the same
		// trace is always the same instead of following the order in which the tags were sampled.
		std::u8string_view current_tracker_name;

		// The report prints the first statistics of the order which -msr asks for, and the count of them is limited by -mst.
		std::size_t printed_count = 0;

		for (auto const& row : BuildReportRows(*this, sort, &resource))
		{
			if (max_tag_count != 0 && printed_count >= max_tag_count)
			{
				break;
			}

			++printed_count;

			auto const& statistic = *row.statistic;

			// The rows are ordered by the tracker and then by the tag, so a tag of the next tracker starts its own block.
			if (current_tracker_name != row.tracker_name)
			{
				current_tracker_name = row.tracker_name;
				print_line(std::format(L"\nTracker:<{}>:", format_name(current_tracker_name)));
			}

			print_line(std::format(
				L"        Tracker:<{}>, MemoryTag:<{}>",
				format_name(row.tracker_name),
				format_name(row.tag_name)
			));

			// Every value is a size in bytes, so it is written with the unit which keeps it readable, and the count is
			// written next to the mean so the mean can be weighed by the number of the snapshots which it came from.
			print_line(std::format(
				L"            Count:<{}>, First:[{}]=<{}>, Last:[{}]=<{}>, Max:[{}]=<{}>, Min:[{}]=<{}>, Mean:<{}>",
				statistic.count,
				FormatTimePoint(statistic.first_time),
				format_size(static_cast<long double>(statistic.first_value)),
				FormatTimePoint(statistic.last_time),
				format_size(static_cast<long double>(statistic.last_value)),
				FormatTimePoint(statistic.max_time),
				format_size(static_cast<long double>(statistic.max_value)),
				FormatTimePoint(statistic.min_time),
				format_size(static_cast<long double>(statistic.min_value)),
				format_size(statistic.sum / static_cast<long double>(statistic.count))
			));

			// The trend of the tag: the part of the time line between the first snapshot of the largest value and the first
			// snapshot of the smallest one, with the value at 20%, 40%, 60% and 80% of it and the slope of every one of those
			// parts, which is a size in bytes per second.
			TagTrend trend = ComputeTrend(statistic);

			if (trend.sample_count < 2)
			{
				print_line(std::format(
					L"            Trend: Interval:[{}, {}], Sample <{}>, no slope",
					FormatTimePoint(trend.begin_time),
					FormatTimePoint(trend.end_time),
					trend.sample_count
				));
			}
			else {
				print_line(std::format(
					L"            Trend: Interval:[{}, {}], Sample <{}>, Slope StdDev:<{}>",
					FormatTimePoint(trend.begin_time),
					FormatTimePoint(trend.end_time),
					trend.sample_count,
					FormatSlope(trend.slope_std_dev)
				));

				for (std::size_t point_index = 0; point_index < trend.time_points.size(); ++point_index)
				{
					print_line(std::format(
						L"                {}%: [{}]=<{}>, Slope:<{}>",
						(point_index + 1) * 20,
						FormatTimePoint(trend.time_points[point_index]),
						format_size(static_cast<long double>(trend.values[point_index])),
						FormatSlope(trend.slopes[point_index])
					));
				}
			}
		}

		// The report tells which count of the statistics it printed when the limit left the rest out, so a reader does not
		// mistake a limited report for a report of every selected tag.
		if (max_tag_count != 0 && printed_count < tag_statistics.size())
		{
			print_line(std::format(
				L"    The report carries the first <{}> statistic(s) of <{}>, use -mst to print more of them",
				printed_count,
				tag_statistics.size()
			));
		}

		return true;
	}

	auto MemoryStatisticParser::ComputeTrend(TagStatistic const& statistic) -> TagTrend
	{
		TagTrend trend;

		if (statistic.samples.empty())
		{
			return trend;
		}

		// The trend is the interval between the first snapshot of the largest value and the first snapshot of the smallest
		// one, which is the part of the time line where the value crossed the whole range of the tag: the tag is reported
		// from the first time it reached its peak to the first time it reached its bottom, whichever came first.
		auto const first_max = std::find_if(statistic.samples.begin(), statistic.samples.end(), [&statistic](ValueSample const& sample) {
			return sample.value == statistic.max_value;
		});

		auto const first_min = std::find_if(statistic.samples.begin(), statistic.samples.end(), [&statistic](ValueSample const& sample) {
			return sample.value == statistic.min_value;
		});

		if (first_max == statistic.samples.end() || first_min == statistic.samples.end())
		{
			return trend;
		}

		ValueSample const& max_sample = *first_max;
		ValueSample const& min_sample = *first_min;

		// The interval is the one which begins earlier and ends later, so the direction of the values inside it is the
		// direction which the tag really took between its two extremes.
		bool is_max_first = max_sample.time <= min_sample.time;
		trend.begin_time = is_max_first ? max_sample.time : min_sample.time;
		trend.begin_value = is_max_first ? max_sample.value : min_sample.value;
		trend.end_time = is_max_first ? min_sample.time : max_sample.time;
		trend.end_value = is_max_first ? min_sample.value : max_sample.value;

		// The samples of the interval, ordered by the time, are the ones which the four points are picked from.
		std::pmr::vector<ValueSample> ordered(&resource);
		ordered.reserve(statistic.samples.size());

		for (auto const& sample : statistic.samples)
		{
			if (trend.begin_time <= sample.time && sample.time <= trend.end_time)
			{
				ordered.push_back(sample);
			}
		}

		std::sort(ordered.begin(), ordered.end(), [](ValueSample const& left, ValueSample const& right) {
			return left.time < right.time;
		});

		trend.sample_count = ordered.size();

		if (ordered.size() < 2)
		{
			// An interval which carries a single sample has no slope at all, which is what a tag whose value never changed
			// between its two extremes is: every point of the interval is that sample and the slopes stay zero. A tag which the
			// tracker sampled once has the same shape, so the four points carry the value of the only snapshot.
			if (!ordered.empty())
			{
				for (std::size_t point_index = 0; point_index < trend.time_points.size(); ++point_index)
				{
					trend.time_points[point_index] = ordered.front().time;
					trend.values[point_index] = ordered.front().value;
				}
			}

			return trend;
		}

		double span = (trend.end_time - trend.begin_time).count();
		if (span <= 0.0)
		{
			// Every sample of the interval carries the same time, which a trace with one snapshot does, so the four points are
			// the samples themselves and the slopes stay zero.
			return trend;
		}

		// The value which the interval carries at a ratio of its length: the sample which is nearest to that time point, which
		// is the same rule as the one of the time line of -mv, so a tag which is sampled densely is not reported with the
		// samples of an even grid which the snapshot interval does not carry.
		auto value_at_ratio = [&ordered, &trend, span](double ratio) {
			DurationSec target = trend.begin_time + DurationSec{ span * ratio };

			auto after = std::lower_bound(ordered.begin(), ordered.end(), target, [](ValueSample const& sample, DurationSec time) {
				return sample.time < time;
			});

			if (after == ordered.end())
			{
				return ordered.back();
			}

			if (after == ordered.begin())
			{
				return ordered.front();
			}

			std::size_t next_index = static_cast<std::size_t>(after - ordered.begin());
			std::size_t previous_index = next_index - 1;

			DurationSec next_distance = ordered[next_index].time - target;
			DurationSec previous_distance = target - ordered[previous_index].time;

			return previous_distance <= next_distance ? ordered[previous_index] : ordered[next_index];
		};

		ValueSample previous_sample{ trend.begin_time, trend.begin_value };

		for (std::size_t point_index = 0; point_index < trend.time_points.size(); ++point_index)
		{
			double ratio = 0.2 * static_cast<double>(point_index + 1);
			ValueSample sample = value_at_ratio(ratio);

			trend.time_points[point_index] = sample.time;
			trend.values[point_index] = sample.value;

			// The slope of the point is the one of the part of the interval which ends at it, in bytes per second: the value
			// which the interval gained from the point before it, divided by the time which that part took.
			double seconds = (sample.time - previous_sample.time).count();

			trend.slopes[point_index] = seconds > 0.0
				? static_cast<double>(sample.value - previous_sample.value) / seconds
				: 0.0;

			previous_sample = sample;
		}

		// The standard deviation of the four slopes tells whether the values of the tag grow at a steady speed: a tag which is
		// filled by a steady stream has a small deviation, and a tag which jumps once has a large one.
		double slope_sum = 0.0;
		for (double slope : trend.slopes)
		{
			slope_sum += slope;
		}

		double slope_mean = slope_sum / static_cast<double>(trend.slopes.size());
		double square_sum = 0.0;

		for (double slope : trend.slopes)
		{
			double difference = slope - slope_mean;
			square_sum += difference * difference;
		}

		trend.slope_std_dev = SqrtValue(square_sum / static_cast<double>(trend.slopes.size()));

		return trend;
	}


	bool MemoryStatisticParser::WriteCSV()
	{
		if (!csv_path)
		{
			return false;
		}

		// The file is created and its previous content is emptied, the same as the report of -o, so the file always carries
		// the statistics of one analysis.
		Potato::Document::DocumentWriter writer{
			*csv_path,
			Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY
		};

		if (!writer)
		{
			// The error is printed through the report printer instead of the pattern log, whose formatter resolves the string
			// wrapper of a message ambiguously in this translation unit: the message is formatted here and handed over as text.
			PrintReportLine(
				*Potato::Log::GetSTDOutPrinter(),
				std::format(L"Cannot open the CSV file <{}>", csv_path->generic_wstring())
			);
			return false;
		}

		std::u8string content;
		CSVRowWriter row{ content };

		// The header names every column, and the names of the value columns carry the unit of the values, which is the byte:
		// the rows are computed from the numbers, so the text report is the place where the unit is written in the cell.
		row.Text(u8"Tracker");
		row.Text(u8"MemoryTag");
		row.Text(u8"Count");
		row.Text(u8"FirstTime_s");
		row.Text(u8"FirstValue_B");
		row.Text(u8"LastTime_s");
		row.Text(u8"LastValue_B");
		row.Text(u8"MaxTime_s");
		row.Text(u8"MaxValue_B");
		row.Text(u8"MinTime_s");
		row.Text(u8"MinValue_B");
		row.Text(u8"Mean_B");
		row.Text(u8"TrendStart_s");
		row.Text(u8"Trend20_s");
		row.Text(u8"Trend20_B");
		row.Text(u8"Trend40_s");
		row.Text(u8"Trend40_B");
		row.Text(u8"Trend60_s");
		row.Text(u8"Trend60_B");
		row.Text(u8"Trend80_s");
		row.Text(u8"Trend80_B");
		row.Text(u8"TrendEnd_s");
		row.Text(u8"Slope20_Bps");
		row.Text(u8"Slope40_Bps");
		row.Text(u8"Slope60_Bps");
		row.Text(u8"Slope80_Bps");
		row.Text(u8"SlopeStdDev_Bps");
		row.EndLine();

		// The CSV file is ordered by the tracker and then by the tag, whatever order the text report uses: the file is read by
		// a spreadsheet or a script which sorts the rows itself when it needs another order, and the order of the data keeps
		// the rows of one tracker together.
		std::size_t printed_count = 0;

		for (auto const& report_row : BuildReportRows(*this, SortKind::Name, &resource))
		{
			if (max_tag_count != 0 && printed_count >= max_tag_count)
			{
				break;
			}

			++printed_count;

			auto const& statistic = *report_row.statistic;
			TagTrend trend = ComputeTrend(statistic);

			row.Text(report_row.tracker_name);
			row.Text(report_row.tag_name);
			row.Integer(statistic.count);
			row.Seconds(statistic.first_time);
			row.Bytes(statistic.first_value);
			row.Seconds(statistic.last_time);
			row.Bytes(statistic.last_value);
			row.Seconds(statistic.max_time);
			row.Bytes(statistic.max_value);
			row.Seconds(statistic.min_time);
			row.Bytes(statistic.min_value);
			row.Decimal(statistic.sum / static_cast<long double>(statistic.count));

			row.Seconds(trend.begin_time);

			for (std::size_t point_index = 0; point_index < trend.time_points.size(); ++point_index)
			{
				row.Seconds(trend.time_points[point_index]);
				row.Bytes(trend.values[point_index]);
			}

			row.Seconds(trend.end_time);

			for (double slope : trend.slopes)
			{
				row.Decimal(static_cast<long double>(slope));
			}

			row.Decimal(static_cast<long double>(trend.slope_std_dev));
			row.EndLine();
		}

		writer.Write(std::span<std::byte const>{
			reinterpret_cast<std::byte const*>(content.data()),
			content.size()
		});

		return true;
	}
}
