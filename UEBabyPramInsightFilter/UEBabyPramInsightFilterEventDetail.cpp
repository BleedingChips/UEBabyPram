module;

#include <cassert>

module UEBabyPramInsightFilterEventDetail;

import UEBabyPramInsightFilterEventDetail;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserContextSwitchStreamer;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	EventDetailParser::EventDetailParser(Config config)
		: resource(config.resource)
		, context_switch_streamer(ContextSwitchStreamer::Config{ 3, DurationSec{ 1 }, &resource })
		, events_manager(EventSpecManager::Config{ false, &resource })
		, query_mapping(EventQueryMapping::Config{ { std::move(config.query) }, &resource })
		, stored_views(&resource)
		, query_text(std::move(config.query_text))
		, max_view_count(config.max_view_count)
		, max_node_count(config.max_node_count)
		, max_depth(config.max_depth)
	{
		// The query is built and checked by the creator of the config, so the parser only takes the one
		// which is ready to be analyzed.
		auto queries = query_mapping.GetQueries();
		assert(queries.size() == 1);
	}

	void EventDetailParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		// The thread condition of the query is resolved from the name of the thread once, so the
		// stack tree only looks the thread of the tree up in the query, and IsThreadRequired() is
		// answered from the same ids.
		query_mapping.AddThread(thread_id, thread_system_id, thread_name);
	}

	void EventDetailParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// Every event spec is stored, the name of an event is resolved from the manager when a saved
		// view is reported, and the events are resolved by the query system as well, so the stack tree
		// is matched by ids instead of by names.
		if (events_manager.AddEvent(id, event_name, file_name, file_line))
		{
			query_mapping.AddEvent(id, event_name);
		}
	}

	void EventDetailParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, its name is the only
		// information of the spec. The name is already UTF-8, which is what the matcher needs.
		if (events_manager.AddEvent(id, event_name, {}, 0))
		{
			query_mapping.AddEvent(id, event_name);
		}
	}

	bool EventDetailParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the name of every event, the metadata carries the spec of the event of
		// the CPU scope logger, the CPU event carries the stack tree which is saved, and the context
		// switch event carries the time which the thread spent off the CPU.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT
			|| flag == ParserRequireFlag::CONTEXT_SWITCH;
	}

	bool EventDetailParser::IsThreadRequired(ThreadID thread_id) const
	{
		return query_mapping.IsThreadMatched(thread_id);
	}

	bool EventDetailParser::IsThreadRequired(ThreadSystemID thread_system_id) const
	{
		// The events which are not carried by the stack tree carry the system thread id of the thread, ex: the
		// context switch event, so the id of the thread is looked up by it first, and a thread which is not
		// discovered yet carries no info which the query could be matched with.
		auto thread_info = GetThreadInfo(thread_system_id);
		if (!thread_info)
		{
			return false;
		}

		// The thread of the stack tree is resolved by the id of the thread, so the rest of the answer is the same as
		// the answer of the thread id.
		return IsThreadRequired(thread_info->thread_id);
	}

	void EventDetailParser::ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration)
	{
		// The context switch events are streamed to the listeners of the saved stack trees, which have
		// been created with the time range of their stack trees.
		context_switch_streamer.AddContextSwitchEvent(thread_id, core_name, duration);
	}

	void EventDetailParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (max_view_count == 0)
		{
			return;
		}

		// The query system has already filtered the thread and the time of the query, and it tells whether the whole
		// call chain of the query is completed inside the tree, which is what the tree is kept by. The chain may carry
		// no statistic element, so the answer does not depend on the matches, and the chain only has to be completed
		// once: the judge stops at the first completion instead of enumerating every match of the tree.
		//
		// The report of this parser is the whole tree, so the tree itself is the event which the time conditions are
		// applied to: the tree is kept when one of the slices of the Time condition overlaps it, and, when the query
		// carries the ITime condition, only when one of the slices of that condition contains the whole tree, which is
		// the same condition as the one the collected occurrences are selected by.
		auto matched_query = query_mapping.FindMatchedQuery(event_scope);
		if (!matched_query)
		{
			return;
		}

		auto time_range = event_scope.GetTimeRange();
		if (!time_range)
		{
			return;
		}

		if (auto queries = query_mapping.GetQueries(); *matched_query < queries.size())
		{
			if (!query_mapping.IsTreeInsideTime(*matched_query, *time_range))
			{
				return;
			}
		}

		DurationSec duration = time_range->Size();

		// Config::max_view_count is the count of the longest calls which are kept: while the stored views are fewer
		// than it the view is stored, and after that the view is only stored when it is longer than the shortest of
		// the stored ones, which is dropped for it.
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

		// The view of the stack tree points into the storage of the analysis, which is reused after the
		// stack tree is analyzed, so the events are copied into the storage of the parser.
		auto& stored_view = stored_views.emplace_back(&resource);
		stored_view.duration = duration;
		stored_view.thread_id = event_scope.thread_id;
		stored_view.system_thread_id = event_scope.system_thread_id;
		stored_view.frame_count = event_scope.frame_count;

		// The listener collects the context switch events of the time range of the stack tree, the
		// blocks which are already completed are given to it as well, so the time which the thread
		// spent off the CPU is available even when the context switch events of the range precede the
		// stack tree.
		stored_view.context_switch_listener = context_switch_streamer.AddListener(event_scope.system_thread_id, *time_range);

		stored_view.events.assign(event_scope.view.begin(), event_scope.view.end());
	}

	void EventDetailParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// The block which the streamer is still writing is completed and given to the listeners, so
		// they carry the context switch events at the end of the trace as well.
		context_switch_streamer.Flush();
	}

	bool EventDetailParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventDetailParser Output:">(printer);

		// The context switch time of a node is the time which the thread spent out of the CPU inside it, so a trace
		// which carries no context switch event carries no such time, and the report tells it instead of leaving the
		// CS column out as if the thread was never switched out.
		if (!context_switch_streamer.HasContextSwitchEvent())
		{
			OutputNoContextSwitchEventNote(printer);
		}
		OutputLog<L"    (View: E: Elapsed Time of the whole stack tree; Node: D: Duration of the event, E: Exclude Time of the event, CS: Context Switch Time of the Exclude Time, it is only printed when the thread spent time off the CPU in the exclude time; TR: Time Range)">(
			printer
		);
		OutputLog<L"    Query: <{}>">(printer, std::u8string_view{ query_text });

		// One event of the saved stack tree and the children of it.
		struct PrintNode
		{
			ThreadCPUEventView::EventView view;
			std::vector<std::size_t> children;
		};

		// A time which is smaller than one nanosecond is treated as zero.
		constexpr double context_switch_epsilon = 1.0e-9;

		auto const queries = query_mapping.GetQueries();

		// A query which carries an element whose timing is collected reports only the events which that
		// element captured and the subtree of the last captured event; a query which collects nothing
		// only selects the stack trees, so the whole tree of every kept stack tree is reported.
		bool has_capture = !queries.empty() && query_mapping.HasCapture(0);

		for (std::size_t view_index = 0; view_index < stored_views.size(); ++view_index)
		{
			auto const& stored_view = stored_views[view_index];

			// The time ranges only decide which stack trees are kept, not which events of a kept tree are
			// reported, so the report of this view is not cut to the ranges whichever report it carries.
			std::string_view thread_name{ "<Unknow>" };
			if (auto thread_info = GetThreadInfo(stored_view.thread_id); thread_info && !thread_info->thread_name.empty())
			{
				thread_name = thread_info->thread_name;
			}

			DurationSec begin_time = stored_view.events.empty() ? DurationSec::zero() : stored_view.events.front().time;
			DurationSec end_time = stored_view.events.empty() ? DurationSec::zero() : stored_view.events.back().time;
			Potato::Misc::IndexSpan<DurationSec> view_time_range{ begin_time, end_time };

			OutputLog<L"    View <{}>: TName:<{}>, TR:<[{}, {}]>, E:<{:.3f}ms>">(
				printer,
				view_index,
				thread_name,
				DisplayTimePoint{ begin_time },
				DisplayTimePoint{ end_time },
				view_time_range.Size().count() * 1000.0
			);

			if (stored_view.events.empty())
			{
				continue;
			}

			// The view which is built over the copied events is the source of the time ranges and of the
			// exclude time of the events, the context switch statis of every range is asked from the
			// listener of the stack tree.
			ThreadCPUEventView event_scope{
				stored_view.thread_id,
				stored_view.system_thread_id,
				std::span<ThreadCPUEvent const>{ stored_view.events },
				stored_view.frame_count
			};

			auto context_switch_provider = [&stored_view](Potato::Misc::IndexSpan<DurationSec> range) {
				if (auto& listener = stored_view.context_switch_listener; listener)
				{
					auto statis = listener->GetContextSwitchStatis(range);
					return ThreadCPUEventView::ContextSwitchStatis{ statis.removed_context_switch_time, statis.switch_count };
				}
				return ThreadCPUEventView::ContextSwitchStatis{ range.Size(), 0 };
			};

			// Print one reported tree: the events are in the order of the call, and the depth of an event is
			// its level in the report. The reported events are not always the events of the stack tree: when
			// the query captures the events, the ancestors which it did not capture are left out, so the
			// depth of a reported event is compressed into the levels which are reported.
			auto print_event_tree = [&](std::span<ThreadCPUEventView::EventView const> reported_events)
			{
				if (reported_events.empty())
				{
					return;
				}

				// The tree is built from the reported events: the events are in the call order, so every
				// event is a child of the event which is above it.
				std::vector<PrintNode> nodes;
				std::vector<std::size_t> roots;
				std::vector<std::size_t> node_stack;
				for (auto const& current : reported_events)
				{
					std::size_t node_index = nodes.size();
					nodes.push_back(PrintNode{ current, {} });

					while (!node_stack.empty() && nodes[node_stack.back()].view.depth >= current.depth)
					{
						node_stack.pop_back();
					}

					if (node_stack.empty())
					{
						roots.push_back(node_index);
					}
					else {
						nodes[node_stack.back()].children.push_back(node_index);
					}

					node_stack.push_back(node_index);
				}

				// The tree is printed with box drawing characters. The exclude time of an event is the time
				// which it consumed outside of its children, and the context switch time of the exclude time
				// is the part of it which the thread spent off the CPU, it is only printed when it is not
				// zero.
				// The tree is printed up to the limits of the report: the nodes over the count of the nodes and the
				// nodes which are deeper than the depth limit are not printed, their place is marked with '...'. The
				// mark of the nodes which are left by the count is printed once, because the rest of the tree is left as
				// well, while the mark of the nodes which are left by the depth is printed once per level.
				std::vector<bool> is_ellipsis_printed;
				std::size_t printed_node_count = 0;
				bool is_node_count_marked = false;
				std::function<void(std::size_t, std::wstring const&, bool, bool, std::size_t)> print_node =
					[&](std::size_t node_index, std::wstring const& prefix, bool is_last, bool is_root, std::size_t level)
				{
					auto const& node = nodes[node_index];

					std::wstring line_prefix;
					if (!is_root)
					{
						line_prefix = prefix + (is_last ? L"\u2514\u2500 " : L"\u251C\u2500 ");
					}

					// The nodes over the count are not printed, and the rest of the tree is left with them, so the mark of
					// the count is printed once.
					if (max_node_count != 0 && printed_node_count >= max_node_count)
					{
						if (!is_node_count_marked)
						{
							is_node_count_marked = true;
							OutputLog<L"        {}...">(printer, line_prefix);
						}
						return;
					}

					if (is_ellipsis_printed.size() <= level)
					{
						is_ellipsis_printed.resize(level + 1, false);
					}

					// The nodes which are deeper than the depth limit are not printed, the mark of such a level is printed
					// once, and it is printed again only when a printed node of the same level comes before it.
					if (max_depth != 0 && level > max_depth)
					{
						if (!is_ellipsis_printed[level])
						{
							is_ellipsis_printed[level] = true;
							OutputLog<L"        {}...">(printer, line_prefix);
						}
						return;
					}

					is_ellipsis_printed[level] = false;
					++printed_node_count;
					std::u8string_view event_name = events_manager.GetEventSpec(node.view.event_id).name_view;

					// The exclude time is the one of the event in the whole stack tree, so it excludes every
					// child of the event, including the children which are not reported; the time of such a
					// child is not a part of the report.
					auto exclude_time = event_scope.GetExcludeTime(node.view, context_switch_provider);
					DurationSec exclude = exclude_time ? exclude_time->active_time : DurationSec::zero();
					DurationSec exclude_without_context_switch = exclude_time ? exclude_time->active_time_without_context_switch : exclude;
					DurationSec context_switch_time = exclude - exclude_without_context_switch;

					if (context_switch_time.count() > context_switch_epsilon)
					{
						OutputLog<L"        {}<{}>, TR:<[{}, {}]>, D:<{:.3f}ms>, E:<{:.3f}ms>, CS:<{:.3f}ms>">(
							printer,
							line_prefix,
							event_name,
							DisplayTimePoint{ node.view.time_range.Begin() },
							DisplayTimePoint{ node.view.time_range.End() },
							node.view.time_range.Size().count() * 1000.0,
							exclude.count() * 1000.0,
							context_switch_time.count() * 1000.0
						);
					}
					else {
						OutputLog<L"        {}<{}>, TR:<[{}, {}]>, D:<{:.3f}ms>, E:<{:.3f}ms>">(
							printer,
							line_prefix,
							event_name,
							DisplayTimePoint{ node.view.time_range.Begin() },
							DisplayTimePoint{ node.view.time_range.End() },
							node.view.time_range.Size().count() * 1000.0,
							exclude.count() * 1000.0
						);
					}

					std::wstring child_prefix = prefix;
					if (!is_root)
					{
						child_prefix += is_last ? L"   " : L"\u2502  ";
					}

					for (std::size_t child_index = 0; child_index < node.children.size(); ++child_index)
					{
						bool child_is_last = child_index + 1 == node.children.size();
						print_node(node.children[child_index], child_prefix, child_is_last, false, level + 1);
					}
				};

				for (auto root_index : roots)
				{
					print_node(root_index, L"", true, true, 1);
				}
			};

			if (!has_capture)
			{
				// The query only selects the stack trees, so the whole tree of the view is reported.
				std::vector<ThreadCPUEventView::EventView> reported_events;
				std::size_t offset = 0;
				while (true)
				{
					auto current = event_scope.FindNextEvent({}, offset);
					if (!current)
					{
						break;
					}

					reported_events.push_back(current);
					offset = current.index_range.Begin() + 1;
				}

				print_event_tree(reported_events);
				continue;
			}

			// Every completion of the call chain is one report: the events which the elements whose timing
			// is collected matched are reported in the order of the call, and the whole subtree of the last
			// one of them is reported under it. The events which the query did not capture (the query
			// elements of the chain and the ancestors of the first captured event) are left out.
			struct CapturedGroup
			{
				std::vector<ThreadCPUEventView::EventView> captured;
			};

			std::vector<CapturedGroup> groups;
			query_mapping.ForEachMatchedCapture(event_scope, [&groups](std::span<EventQueryMapping::CaptureMatch const> matches)
			{
				auto& group = groups.emplace_back();
				group.captured.reserve(matches.size());
				for (auto const& match : matches)
				{
					group.captured.push_back(match.event_view);
				}
			});

			for (std::size_t group_index = 0; group_index < groups.size(); ++group_index)
			{
				auto const& captured = groups[group_index].captured;
				assert(!captured.empty());

				// The captured events are the first levels of the report, the ancestors which the query did
				// not capture are left out, so they are compressed into the levels 1..count.
				std::vector<ThreadCPUEventView::EventView> reported_events;
				reported_events.reserve(captured.size());
				std::u8string chain_text;
				for (std::size_t capture_index = 0; capture_index < captured.size(); ++capture_index)
				{
					auto view = captured[capture_index];
					view.depth = capture_index + 1;
					reported_events.push_back(view);

					if (capture_index != 0)
					{
						chain_text.append(u8" -> ");
					}
					chain_text.append(events_manager.GetEventSpec(view.event_id).name_view);
				}

				// The whole subtree of the last captured event is reported under it, the depth of an event is
				// shifted so that the child of the last captured event is the next level of the report.
				auto const& last_captured = captured.back();
				std::size_t last_captured_depth = captured.size();
				std::size_t offset = last_captured.index_range.Begin() + 1;
				while (offset < last_captured.index_range.End())
				{
					auto descendant = event_scope.FindNextEvent({}, { offset, last_captured.index_range.End() });
					if (!descendant)
					{
						break;
					}

					auto view = descendant;
					view.depth = last_captured_depth + (descendant.depth - last_captured.depth);
					reported_events.push_back(view);
					offset = descendant.index_range.Begin() + 1;
				}

				OutputLog<L"        Match <{}>: <{}>">(printer, group_index, std::u8string_view{ chain_text });
				print_event_tree(reported_events);
			}
		}

		return true;
	}
}