module;

#include <cassert>
#include <cmath>

module UEBabyPramInsightFilterFlameGraph;

import UEBabyPramInsightFilterFlameGraph;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	namespace
	{
		// Append the name of one frame to the call path of a folded stack line: the delimiter of the format and the
		// characters which would break the line are replaced in the name, so one line always carries one call path.
		void AppendFoldedFrameName(std::u8string& path, std::u8string_view frame_name)
		{
			for (char8_t character : frame_name)
			{
				switch (character)
				{
				case u8';':
					path.push_back(u8':');
					break;
				case u8'\n':
				case u8'\r':
				case u8'\t':
					path.push_back(u8' ');
					break;
				default:
					path.push_back(character);
					break;
				}
			}
		}

		// The weight of one line of the folded stack file, which is the time which the call path took on its own in
		// microseconds, the unit which the flame graph tools read the folded stacks in.
		long long FoldedStackWeight(DurationSec duration)
		{
			return static_cast<long long>(std::llround(duration.count() * 1'000'000.0));
		}
	}

	EventFlameGraphParser::EventFlameGraphParser(Config config)
		: resource(config.resource)
		, events_manager(EventSpecManager::Config{ false, &resource })
		, query_mapping(EventQueryMapping::Config{ { std::move(config.query) }, &resource })
		, nodes(&resource)
		, query_text(std::move(config.query_text))
		, stored_views(&resource)
		, max_view_count(config.max_view_count)
		, folded_stack_path(std::move(config.folded_stack_path))
	{
		// The query is built and checked by the creator of the config, so the parser only takes the one
		// which is ready to be analyzed.
		auto queries = query_mapping.GetQueries();
		assert(queries.size() == 1);

		// The synthetic root of the graph, its children are the roots of the matched stack trees.
		nodes.emplace_back(&resource);
	}

	void EventFlameGraphParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		// The thread condition of the query is resolved from the name of the thread once, so the
		// stack tree only looks the thread of the tree up in the query, and IsThreadRequired() is
		// answered from the same ids.
		query_mapping.AddThread(thread_id, thread_system_id, thread_name);
	}

	void EventFlameGraphParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// Every event spec is stored, the name of an event is resolved from the manager when the flame
		// graph is reported, and the events are resolved by the query system as well, so the stack tree
		// is matched by ids instead of by names.
		if (events_manager.AddEvent(id, event_name, file_name, file_line))
		{
			query_mapping.AddEvent(id, event_name);
		}
	}

	void EventFlameGraphParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, its name is the only
		// information of the spec. The name is already UTF-8, which is what the matcher needs.
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			query_mapping.AddEvent(id, event_name);
		}
	}

	bool EventFlameGraphParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the name of every event, the metadata carries the spec of the event of
		// the CPU scope logger, and the CPU event carries the stack tree which is aggregated.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT;
	}

	bool EventFlameGraphParser::IsThreadRequired(ThreadID thread_id) const
	{
		return query_mapping.IsThreadMatched(thread_id);
	}

	bool EventFlameGraphParser::IsThreadRequired(ThreadSystemID thread_system_id) const
	{
		// The events which are not carried by the stack tree carry the system thread id of the thread, so
		// the id of the thread is looked up by it first, and a thread which is not discovered yet carries
		// no info which the query could be matched with.
		auto thread_info = GetThreadInfo(thread_system_id);
		if (!thread_info)
		{
			return false;
		}

		return IsThreadRequired(thread_info->thread_id);
	}

	void EventFlameGraphParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (event_scope.view.empty())
		{
			return;
		}

		if (max_view_count == 0)
		{
			return;
		}

		// The query system has already filtered the thread and the time of the query, and it tells whether the whole
		// call chain of the query is completed inside the tree, which is what the tree is aggregated by. The chain may
		// carry no statistic element, so the answer does not depend on the matches, and the chain only has to be
		// completed once: the judge stops at the first completion instead of enumerating every match of the tree. The
		// query which keeps the tree is recorded with it, so the report of its call paths is limited by the time ranges
		// of that query.
		auto matched_query = query_mapping.FindMatchedQuery(event_scope);
		if (!matched_query)
		{
			return;
		}

		std::size_t query_index = *matched_query;

		auto time_range = event_scope.GetTimeRange();
		if (!time_range)
		{
			return;
		}

		DurationSec duration = time_range->Size();

		// Config::max_view_count is the count of the longest stack trees which are aggregated: while the kept views
		// are fewer than it the view is kept, and after that the view is only kept when it is longer than the shortest
		// of the kept ones, which is dropped for it, the same as EventDetailParser.
		if (stored_views.size() >= max_view_count)
		{
			auto shortest = std::min_element(
				stored_views.begin(),
				stored_views.end(),
				[](StoredView const& left, StoredView const& right) {
					return left.duration < right.duration;
				});

			if (shortest == stored_views.end() || shortest->duration >= duration)
			{
				return;
			}

			stored_views.erase(shortest);
		}

		// The view of the stack tree points into the storage of the analysis, which is reused after the stack tree is
		// analyzed, so the events are copied and they are aggregated when the whole trace is analyzed.
		auto& stored_view = stored_views.emplace_back(&resource);
		stored_view.duration = duration;
		stored_view.query_index = query_index;
		stored_view.thread_id = event_scope.thread_id;
		stored_view.system_thread_id = event_scope.system_thread_id;
		stored_view.frame_count = event_scope.frame_count;
		stored_view.events.assign(event_scope.view.begin(), event_scope.view.end());
	}

	void EventFlameGraphParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// The kept stack trees are aggregated into the flame graph, they are the longest ones which the query
		// matches, which are complete now.
		for (auto& stored_view : stored_views)
		{
			AggregateStackTree(ThreadCPUEventView{
				stored_view.thread_id,
				stored_view.system_thread_id,
				std::span<ThreadCPUEvent const>{ stored_view.events },
				stored_view.frame_count
			},
			stored_view.query_index
		);
		}
	}

	void EventFlameGraphParser::AggregateStackTree(ThreadCPUEventView event_scope, std::size_t query_index)
	{
		// The time ranges of the query only decide which stack trees are aggregated, not which events of an
		// aggregated stack tree are counted: a stack tree which overlaps one of the ranges is aggregated whole, so
		// every event which it carries is counted, and the time of an event is never cut to the ranges. Only the
		// events which overlap one of the ranges are printed later, see PrintToLog(): the events which are out of
		// the ranges are left out of the report, while their time is still a part of the inclusive time of the node
		// which holds them.
		std::span<Potato::Misc::IndexSpan<DurationSec> const> query_time_ranges;
		if (auto queries = query_mapping.GetQueries(); query_index < queries.size())
		{
			query_time_ranges = queries[query_index].time_ranges;
		}

		auto is_in_query_time_range = [query_time_ranges](Potato::Misc::IndexSpan<DurationSec> time_range) {
			if (query_time_ranges.empty())
			{
				return true;
			}

			for (auto& query_time_range : query_time_ranges)
			{
				if (query_time_range.IsOverlap(time_range))
				{
					return true;
				}
			}

			return false;
		};

		// The current call path: the depth of every level and the index of its node. The events are
		// stored in the call order, so an event is a child of the event which is above it.
		std::vector<std::pair<std::size_t, std::size_t>> path;

		std::size_t offset = 0;
		while (true)
		{
			auto current = event_scope.FindNextEvent({}, offset);
			if (!current)
			{
				break;
			}

			offset = current.index_range.Begin() + 1;

			while (!path.empty() && path.back().first >= current.depth)
			{
				path.pop_back();
			}

			std::size_t parent_index = path.empty() ? 0 : path.back().second;

			std::size_t child_index = std::numeric_limits<std::size_t>::max();
			for (auto index : nodes[parent_index].children)
			{
				if (nodes[index].event_id == current.event_id)
				{
					child_index = index;
					break;
				}
			}

			if (child_index == std::numeric_limits<std::size_t>::max())
			{
				child_index = nodes.size();
				nodes.emplace_back(&resource);
				nodes[child_index].event_id = current.event_id;
				nodes[parent_index].children.push_back(child_index);
			}

			// The inclusive time of the call path is the duration of the event, which is the time from
			// its beginning to the end of its subtree.
			nodes[child_index].total_duration += current.time_range.Size();

			// The occurrence is counted whether it is in the time ranges of the query or not, it only decides
			// whether the node is printed: a node is printed when at least one of its occurrences is in the
			// ranges, then the whole time of the node is printed.
			nodes[child_index].has_occurrence_in_query_time_range =
				nodes[child_index].has_occurrence_in_query_time_range || is_in_query_time_range(current.time_range);

			path.emplace_back(current.depth, child_index);
		}
	}

	bool EventFlameGraphParser::WriteFoldedStack() const
	{
		assert(folded_stack_path.has_value());

		// The file is created and its previous content is emptied, the same as the report of -o, so the folded stack
		// file always carries the flame graph of one analysis.
		Potato::Document::DocumentWriter writer{
			*folded_stack_path,
			Potato::Document::DocumentWriter::OpenMode::CREATE_OR_EMPTY
		};

		if (!writer)
		{
			Potato::Log::Log<L"Cannot open the folded stack file <{}>">(folded_stack_path->generic_wstring());
			return false;
		}

		std::u8string content;

		// The call paths are visited in the order of the text report, the children of a node are visited from the
		// longest one, so the file of the same trace is always the same.
		std::function<void(std::size_t, std::u8string const&)> write_node =
			[&](std::size_t node_index, std::u8string const& parent_path)
		{
			auto const& node = nodes[node_index];

			std::u8string path = parent_path;
			if (!path.empty())
			{
				path.push_back(u8';');
			}
			AppendFoldedFrameName(path, events_manager.GetEventSpec(node.event_id).name_view);

			// The time which the call path took on its own is the inclusive time of the node without the inclusive time
			// of its children, so the weight of a line is never counted again by another line. The children which the
			// time ranges of the query left out are counted as the time of this path, the same as the text report,
			// which leaves them out of the report as well.
			DurationSec children_duration = DurationSec::zero();

			std::vector<std::size_t> printed_children;
			printed_children.reserve(node.children.size());
			for (auto child_index : node.children)
			{
				if (nodes[child_index].has_occurrence_in_query_time_range)
				{
					printed_children.push_back(child_index);
					children_duration += nodes[child_index].total_duration;
				}
			}

			// A call path which took no time on its own, because all of its time is in its children, carries no line:
			// the format only carries the time of a path, and a line of zero would only widen the graph.
			if (long long weight = FoldedStackWeight(node.total_duration - children_duration); weight > 0)
			{
				std::u8string line = path;
				line.push_back(u8' ');
				std::string encoded_weight = std::format("{}", weight);
				line.append(reinterpret_cast<char8_t const*>(encoded_weight.data()), encoded_weight.size());
				line.push_back(u8'\n');
				content.append(line);
			}

			std::sort(printed_children.begin(), printed_children.end(), [&](std::size_t left, std::size_t right) {
				return nodes[left].total_duration > nodes[right].total_duration;
			});

			for (auto child_index : printed_children)
			{
				write_node(child_index, path);
			}
		};

		// The roots of the flame graph are the roots of the kept stack trees, which overlap the time ranges of the
		// query, the same rule as the text report.
		std::vector<std::size_t> printed_roots;
		printed_roots.reserve(nodes[0].children.size());
		for (auto child_index : nodes[0].children)
		{
			if (nodes[child_index].has_occurrence_in_query_time_range)
			{
				printed_roots.push_back(child_index);
			}
		}

		std::sort(printed_roots.begin(), printed_roots.end(), [&](std::size_t left, std::size_t right) {
			return nodes[left].total_duration > nodes[right].total_duration;
		});

		for (auto root_index : printed_roots)
		{
			write_node(root_index, std::u8string{});
		}

		writer.Write(std::span<std::byte const>{
			reinterpret_cast<std::byte const*>(content.data()),
			content.size()
		});

		return true;
	}

	bool EventFlameGraphParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		// The folded stack file is written instead of the text report: every line of it is one call path which the flame
		// graph tools draw, while the report nests the call paths in a text which only a human reads.
		if (folded_stack_path)
		{
			return WriteFoldedStack();
		}

		OutputLog<L"EventFlameGraphParser Output:">(printer);
		OutputLog<L"    (D: Inclusive Duration of a call path, the sum of the duration of every occurrence of the path; Percent: the time of the path over the whole matched time)">(
			printer
		);
		OutputLog<L"    Query: <{}>">(printer, std::u8string_view{ query_text });

		DurationSec total_duration = DurationSec::zero();
		for (auto child_index : nodes[0].children)
		{
			total_duration += nodes[child_index].total_duration;
		}

		OutputLog<L"    Total: <{:.3f}ms>">(printer, total_duration.count() * 1000.0);

		if (total_duration.count() <= 0.0)
		{
			return true;
		}

		double total_count = total_duration.count();

		// The flame graph is printed with box drawing characters, the children of a node are sorted by
		// the time which they take, so the widest call is the first one, the same as a flame graph.
		// The report is narrower than the aggregation: a node is printed only when one of its occurrences is in the
		// time ranges of the query, the node which only carries the occurrences out of the ranges is left out with
		// its children, while its time is already a part of the inclusive time of the node which holds it. So the
		// time of a printed node is the whole time of its call path, it is never cut to the ranges.
		std::function<void(std::size_t, std::wstring const&, bool, bool)> print_node =
			[&](std::size_t node_index, std::wstring const& prefix, bool is_last, bool is_root)
		{
			auto& node = nodes[node_index];

			std::wstring line_prefix;
			if (!is_root)
			{
				line_prefix = prefix + (is_last ? L"\u2514\u2500 " : L"\u251C\u2500 ");
			}

			std::u8string_view event_name = events_manager.GetEventSpec(node.event_id).name_view;
			double percent = node.total_duration.count() / total_count * 100.0;
			OutputLog<L"        {}<{}>, D:<{:.3f}ms>, Percent:<{:.2f}%>">(
				printer,
				line_prefix,
				event_name,
				node.total_duration.count() * 1000.0,
				percent
			);

			// The children which are out of the time ranges of the query are not printed, so the children which are
			// printed are collected first: the last one of them carries the corner connector.
			std::vector<std::size_t> printed_children;
			printed_children.reserve(node.children.size());
			for (auto child_index : node.children)
			{
				if (nodes[child_index].has_occurrence_in_query_time_range)
				{
					printed_children.push_back(child_index);
				}
			}

			std::sort(printed_children.begin(), printed_children.end(), [&](std::size_t left, std::size_t right) {
				return nodes[left].total_duration > nodes[right].total_duration;
			});

			std::wstring child_prefix = prefix;
			if (!is_root)
			{
				child_prefix += is_last ? L"   " : L"\u2502  ";
			}

			for (std::size_t child_offset = 0; child_offset < printed_children.size(); ++child_offset)
			{
				bool child_is_last = child_offset + 1 == printed_children.size();
				print_node(printed_children[child_offset], child_prefix, child_is_last, false);
			}
		};

		// The roots of the flame graph are the roots of the kept stack trees, which overlap the time ranges of the
		// query, so they are printed, the same rule is applied to them as well.
		std::vector<std::size_t> printed_roots;
		printed_roots.reserve(nodes[0].children.size());
		for (auto child_index : nodes[0].children)
		{
			if (nodes[child_index].has_occurrence_in_query_time_range)
			{
				printed_roots.push_back(child_index);
			}
		}

		std::sort(printed_roots.begin(), printed_roots.end(), [&](std::size_t left, std::size_t right) {
			return nodes[left].total_duration > nodes[right].total_duration;
		});

		for (auto root_index : printed_roots)
		{
			print_node(root_index, L"", true, true);
		}

		return true;
	}
}