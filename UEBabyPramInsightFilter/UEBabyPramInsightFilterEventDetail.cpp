module;

#include <cassert>
#include <compare>

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
	namespace
	{
		// Whether the two time ranges have a common time, the ranges which only touch each other at their end are not
		// overlapped.
		bool IsTimeRangeOverlapped(Potato::Misc::IndexSpan<DurationSec> left, Potato::Misc::IndexSpan<DurationSec> right)
		{
			switch (left <=> right)
			{
			case Potato::Misc::IndexSpanOrdering::LeftOverlapBegin:
			case Potato::Misc::IndexSpanOrdering::RightOverlapEnd:
			case Potato::Misc::IndexSpanOrdering::Contained:
			case Potato::Misc::IndexSpanOrdering::Contains:
			case Potato::Misc::IndexSpanOrdering::Equal:
				return true;
			default:
				return false;
			}
		}
	}

	EventDetailParser::EventDetailParser(Config config)
		: resource(config.resource)
		, context_switch_streamer(ContextSwitchStreamer::Config{ 3, DurationSec{ 1 }, &resource })
		, events_manager(EventSpecManager::Config{ false, &resource })
		, query_mapping(EventQueryMapping::Config{ { std::move(config.query) }, &resource })
		, stored_views(&resource)
		, query_text(std::move(config.query_text))
		, max_view_count(config.max_view_count)
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

		// The query system has already filtered the thread and the time of the query, the stack tree is
		// only kept when at least one event of it completes the chain of the query.
		bool matched = false;
		query_mapping.ForEachMatchedEvent(event_scope, [&](EventQueryMapping::EventMatch const&) {
			matched = true;
		});

		if (!matched)
		{
			return;
		}

		auto time_range = event_scope.GetTimeRange();
		if (!time_range)
		{
			return;
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

		// The time ranges of the query are the target of the report: only the events which intersect one of them are
		// printed, and an event which intersects none of them is omitted. A query which carries no time range reports
		// every event of the saved stack trees.
		std::span<Potato::Misc::IndexSpan<DurationSec> const> target_time_ranges;
		if (auto queries = query_mapping.GetQueries(); !queries.empty())
		{
			target_time_ranges = queries.front().time_ranges;
		}

		auto is_in_target_time_range = [target_time_ranges](Potato::Misc::IndexSpan<DurationSec> time_range) {
			if (target_time_ranges.empty())
			{
				return true;
			}

			for (auto& target_time_range : target_time_ranges)
			{
				if (IsTimeRangeOverlapped(target_time_range, time_range))
				{
					return true;
				}
			}

			return false;
		};

		for (std::size_t view_index = 0; view_index < stored_views.size(); ++view_index)
		{
			auto const& stored_view = stored_views[view_index];

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

			// The stack tree is built from the events of the view: the events are stored in the call
			// order, so every event is a child of the event which is above it, and the end of the stack
			// tree is an event without an id, which is not an event of the tree.
			std::vector<PrintNode> nodes;
			std::vector<std::size_t> roots;
			std::vector<std::size_t> node_stack;
			{
				std::size_t offset = 0;
				while (true)
				{
					auto current = event_scope.FindNextEvent({}, offset);
					if (!current)
					{
						break;
					}

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
					offset = current.index_range.Begin() + 1;
				}
			}

			if (nodes.empty())
			{
				continue;
			}

			// The tree is printed with box drawing characters. The exclude time of an event is the time
			// which it consumed outside of its children, and the context switch time of the exclude time
			// is the part of it which the thread spent off the CPU, it is only printed when it is not
			// zero.
			std::function<void(std::size_t, std::wstring const&, bool, bool)> print_node =
				[&](std::size_t node_index, std::wstring const& prefix, bool is_last, bool is_root)
			{
				auto const& node = nodes[node_index];

				std::wstring line_prefix;
				if (!is_root)
				{
					line_prefix = prefix + (is_last ? L"\u2514\u2500 " : L"\u251C\u2500 ");
				}

				// The event does not intersect the target time range of the report, so neither it nor any of its
				// children is in the target, and only the mark of the omitted children is printed.
				if (!is_in_target_time_range(node.view.time_range))
				{
					OutputLog<L"        {}...">(printer, line_prefix);
					return;
				}

				std::u8string_view event_name = events_manager.GetEventSpec(node.view.event_id).name_view;
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
					print_node(node.children[child_index], child_prefix, child_is_last, false);
				}
			};

			for (auto root_index : roots)
			{
				print_node(root_index, L"", true, true);
			}
		}

		return true;
	}
}
