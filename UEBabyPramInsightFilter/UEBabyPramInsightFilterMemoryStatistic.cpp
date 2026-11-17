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
			// ordered by the data and the report of the same trace never depends on the sort of the storage.
			auto is_name_less = [](ReportRow const& left, ReportRow const& right) {
				if (left.statistic->query_index != right.statistic->query_index)
				{
					return left.statistic->query_index < right.statistic->query_index;
				}

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
		, snapshot_query_indices(&resource)
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

		// The queries which select the tracker and the time point of the snapshot are decided once per snapshot, so the
		// walk of the tags only asks the question of the chain for them.
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

		// Every tag of the snapshot is one unit per query which selects it: the name of the tag is the chain of its
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

				auto& statistic = FindTagStatistic(query_index, tracker_id, tag_id);
				AccumulateStatistic(statistic, time, value);
			}

			return true;
		});
	}

	auto MemoryStatisticParser::FindTagStatistic(std::size_t query_index, MemoryTrackerId tracker_id, MemoryTagId tag_id) -> TagStatistic&
	{
		auto found = std::find_if(tag_statistics.begin(), tag_statistics.end(), [query_index, tracker_id, tag_id](TagStatistic const& statistic) {
			return statistic.query_index == query_index
				&& statistic.tracker_id == tracker_id
				&& statistic.tag_id == tag_id;
		});

		if (found != tag_statistics.end())
		{
			return *found;
		}

		// The statistics of a tag which is sampled for the first time are created here, so the count of the entries is the
		// count of the tags which the queries select.
		return tag_statistics.emplace_back(TagStatistic{ query_index, tracker_id, tag_id });
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
		print_line(L"    (Memory Tag Statistic: the values which the snapshots of one tracker sampled for one memory tag which the query selects)");
		print_line(L"    (First/Last: the first and the last snapshot which carries the tag, with the value of that snapshot; Max/Min: the snapshot of the largest and of the smallest value; Mean: the mean of the values of every snapshot which carries the tag)");

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

		// The statistics are reported by the query which accumulated them, then by the name of the tracker and of the tag,
		// so the report of the same trace is always the same instead of following the order in which the tags were sampled.
		std::size_t current_query_index = std::numeric_limits<std::size_t>::max();

		for (auto const& row : BuildReportRows(*this, sort, &resource))
		{
			auto const& statistic = *row.statistic;

			if (statistic.query_index != current_query_index)
			{
				current_query_index = statistic.query_index;
				// The empty line before the queries separates the block of every query from the one above it.
				print_line(std::format(L"\nQuery <{}>:", current_query_index));
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
				DisplayTimePoint{ statistic.first_time },
				format_size(static_cast<long double>(statistic.first_value)),
				DisplayTimePoint{ statistic.last_time },
				format_size(static_cast<long double>(statistic.last_value)),
				DisplayTimePoint{ statistic.max_time },
				format_size(static_cast<long double>(statistic.max_value)),
				DisplayTimePoint{ statistic.min_time },
				format_size(static_cast<long double>(statistic.min_value)),
				format_size(statistic.sum / static_cast<long double>(statistic.count))
			));
		}

		return true;
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
			Potato::Log::Log<L"Cannot open the CSV file <{}>">(csv_path->generic_wstring());
			return false;
		}

		std::u8string content;
		CSVRowWriter row{ content };

		// The header names every column, and the names of the value columns carry the unit of the values, which is the byte:
		// the rows are computed from the numbers, so the text report is the place where the unit is written in the cell.
		row.Text(u8"QueryIndex");
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
		row.EndLine();

		for (auto const& report_row : BuildReportRows(*this, sort, &resource))
		{
			auto const& statistic = *report_row.statistic;

			row.Integer(statistic.query_index);
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
			row.EndLine();
		}

		writer.Write(std::span<std::byte const>{
			reinterpret_cast<std::byte const*>(content.data()),
			content.size()
		});

		return true;
	}
}
