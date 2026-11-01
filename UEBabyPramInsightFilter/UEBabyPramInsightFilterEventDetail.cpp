module;

#include <cassert>

module UEBabyPramInsightFilterEventDetail;

import UEBabyPramInsightFilterEventDetail;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	EventDetailParser::EventDetailParser(Config config)
		: resource(config.resource)
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
		query_mapping.AddThread(thread_id, thread_name);
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
		// the CPU scope logger, and the CPU event carries the stack tree which is saved.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA
			|| flag == ParserRequireFlag::CPU_EVENT;
	}

	bool EventDetailParser::IsThreadRequired(ThreadID thread_id) const
	{
		return query_mapping.IsThreadMatched(thread_id);
	}

	void EventDetailParser::OnCPUStackTree(ThreadCPUEventView event_scope)
	{
		if (max_view_count == 0 || stored_views.size() >= max_view_count)
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

		// The view of the stack tree points into the storage of the analysis, which is reused after the
		// stack tree is analyzed, so the events are copied into the storage of the parser.
		auto& stored_view = stored_views.emplace_back(&resource);
		stored_view.thread_id = event_scope.thread_id;
		stored_view.system_thread_id = event_scope.system_thread_id;
		stored_view.frame_count = event_scope.frame_count;
		stored_view.events.assign(event_scope.view.begin(), event_scope.view.end());
	}

	bool EventDetailParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		PrintStoredViews(printer);
		return true;
	}

	void EventDetailParser::PrintStoredViews(Potato::Log::LogPrinter& printer) const
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventDetailParser Output:">(printer);
		OutputLog<L"    Query: <{}>">(printer, std::u8string_view{ query_text });

		for (std::size_t view_index = 0; view_index < stored_views.size(); ++view_index)
		{
			auto const& stored_view = stored_views[view_index];

			std::string_view thread_name{ "<Unknow>" };
			if (auto thread_info = GetThreadInfo(stored_view.thread_id); thread_info && !thread_info->thread_name.empty())
			{
				thread_name = thread_info->thread_name;
			}

			std::string frame_count_buffer;
			std::string_view frame_count{ "<none>" };
			if (stored_view.frame_count)
			{
				frame_count_buffer = std::to_string(*stored_view.frame_count);
				frame_count = frame_count_buffer;
			}

			DisplayTimePoint begin_time{ stored_view.events.empty() ? DurationSec::zero() : stored_view.events.front().time };
			DisplayTimePoint end_time{ stored_view.events.empty() ? DurationSec::zero() : stored_view.events.back().time };
			OutputLog<L"    View <{}>: TName:<{}>, Thread:<{}>, SystemThread:<{}>, FrameCount:<{}>, Events:<{}>, TR:<[{}, {}]>">(
				printer,
				view_index,
				thread_name,
				stored_view.thread_id.id,
				stored_view.system_thread_id.id,
				frame_count,
				stored_view.events.size(),
				begin_time,
				end_time
			);

			for (std::size_t event_index = 0; event_index < stored_view.events.size(); ++event_index)
			{
				auto const& event = stored_view.events[event_index];

				// The end of a stack tree is an event without an id, it is the boundary of the view and
				// it carries no name.
				if (!event.event_id)
				{
					continue;
				}

				std::u8string_view event_name = events_manager.GetEventSpec(event.event_id).name_view;
				OutputLog<L"        <{:<4}> Depth:<{}>, Name:<{}>, T:<{}>">(
					printer,
					event_index,
					event.depth,
					event_name,
					DisplayTimePoint{ event.time }
				);
			}
		}
	}
}
