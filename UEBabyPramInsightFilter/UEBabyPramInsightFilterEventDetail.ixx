module;

#include <cassert>

export module UEBabyPramInsightFilterEventDetail;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import UEBabyPramInsightParserContextSwitchStreamer;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Keep a copy of the stack trees which the event query matches, and report the copies when the
	// whole trace is analyzed.
	//
	// The query is the same call chain which EventStatisticParser counts (see EventQuery and
	// GetFilterEbnf()): a stack tree is kept when one of its events completes the chain, the thread
	// condition and the time ranges of the query are matched as well, so a stack tree which the query
	// does not count is not kept.
	//
	// One kept stack tree is one copy (the Config::max_view_count longest ones are kept at most, a
	// shorter stack tree is only kept while there is room for it, or when it is longer than the
	// shortest of the kept ones, which is dropped for it). The events of the stack tree are copied into
	// the storage of the parser, because the view of the stack tree only points into the storage of
	// the analysis, which is reused after the stack tree is analyzed.
	struct EventDetailParser : public Parser
	{
		struct Config
		{
			// The query which the stack trees are kept by, it is built from its text by
			// ParseEventQuery, ex: std::get<EventQuery>(ParseEventQuery(u8"(^Tick^)->[^Update^]")).
			// The command line reports the error of a query which is not acceptable, so the parser
			// only receives a query which is ready to be analyzed.
			EventQuery query;
			// The text which the query is built from, it is only reported with the saved views.
			std::u8string query_text;
			// How many of the longest stack trees are kept at most, every stack tree is one copy, and the shortest
			// one is dropped for a longer one when the count is reached, 0 for none.
			std::size_t max_view_count = 10;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One saved stack tree: the identity of the thread and a copy of the events of the whole stack
		// tree. The view which the parser receives only points into the storage of the analysis, so the
		// events are copied here, and the saved view can be reported after the analysis is done. The
		// listener collects the context switch events of the time range of the stack tree, so the time
		// which the thread spent off the CPU is reported for the view and for every event of it.
		struct StoredView
		{
			StoredView(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: events(resource) {}

			// The duration of the whole stack tree, it is what the longest ones are picked by.
			DurationSec duration = DurationSec::zero();
			ThreadID thread_id;
			ThreadSystemID system_thread_id;
			std::optional<std::size_t> frame_count;
			std::shared_ptr<ContextSwitchStreamer::Listener> context_switch_listener;
			std::pmr::vector<ThreadCPUEvent> events;
		};

		EventDetailParser(Config config = {});
		EventDetailParser(EventDetailParser const&) = delete;
		EventDetailParser(EventDetailParser&&) = default;

		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		// The context switch events are streamed to the listeners of the saved stack trees.
		virtual void ContextSwitchEvent(ThreadSystemID thread_id, std::size_t core_name, Potato::Misc::IndexSpan<DurationSec> duration) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		// Only the threads which the query counts are analyzed, a query which carries a thread
		// condition only counts the threads which its matcher matched. The thread is known by its thread id and by
		// its system thread id, so both of them are answered.
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		virtual bool IsThreadRequired(ThreadSystemID thread_system_id) const override;
		virtual void AllAnalyzeDone() override;
		// Report every saved stack tree with the events which it carries, the analysis has to be done
		// before this is called.
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// The stack trees which are saved, the order is the analysis order.
		std::span<StoredView const> GetStoredViews() const { return stored_views; }

	protected:

		std::pmr::unsynchronized_pool_resource resource;
		// The streamer of the context switch events, every saved stack tree creates a listener for its
		// own time range, so the time which the thread spent off the CPU is available for the view and
		// for every event of it.
		ContextSwitchStreamer context_switch_streamer;
		EventSpecManager events_manager;
		// The query system which is built from Config::query, the ids of the events are resolved into
		// it when they are discovered, so the stack tree is never matched by a name.
		EventQueryMapping query_mapping;
		std::pmr::vector<StoredView> stored_views;
		std::u8string query_text;
		std::size_t max_view_count = 10;
	};
}
