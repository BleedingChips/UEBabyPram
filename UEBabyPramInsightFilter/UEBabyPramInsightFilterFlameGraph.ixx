module;

#include <cassert>

export module UEBabyPramInsightFilterFlameGraph;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventQuery;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Collect the stack trees which the event query matches and report the timing of every call path as
	// a flame graph.
	//
	// A call path is a chain of events from the root of a stack tree to the event itself, and the node of
	// a path carries the sum of the duration of every occurrence of that path in the matched stack
	// trees. The children of a node are the calls which were made inside it, so their sum may be larger
	// than the node itself, which is the inclusive time of the call, the same as a flame graph.
	//
	// The query is the same call chain which EventDetailParser (see EventQuery and GetFilterEbnf()):
	// a stack tree is aggregated when one of its events completes the chain, the thread condition and
	// the time ranges of the query are matched as well.
	struct EventFlameGraphParser : public Parser
	{
		struct Config
		{
			// The query which the stack trees are selected by, it is built from its text by
			// ParseEventQuery, ex: std::get<EventQuery>(ParseEventQuery(u8"(^Tick^)->[^Update^]")).
			// The command line reports the error of a query which is not acceptable, so the parser
			// only receives a query which is ready to be analyzed.
			EventQuery query;
			// The text which the query is built from, it is only reported with the flame graph.
			std::u8string query_text;
			// How many of the longest stack trees are aggregated at most, the stack tree which is kept is dropped for a
			// longer one when the count is reached, 0 for none. It is the same option with the same meaning as
			// EventDetailParser::Config::max_view_count.
			std::size_t max_view_count = 10;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		EventFlameGraphParser(Config config = {});
		EventFlameGraphParser(EventFlameGraphParser const&) = delete;
		EventFlameGraphParser(EventFlameGraphParser&&) = default;

		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		// Only the threads which the query counts are analyzed, a query which carries a thread
		// condition only counts the threads which its matcher matched. The thread is known by its
		// thread id and by its system thread id, so both of them are answered.
		virtual bool IsThreadRequired(ThreadID thread_id) const override;
		virtual bool IsThreadRequired(ThreadSystemID thread_system_id) const override;
		// The stack trees which are kept are aggregated here, the aggregation of a stack tree needs its events, which
		// are only complete after the whole trace is analyzed together with the longest ones which are kept.
		virtual void AllAnalyzeDone() override;
		// Report the flame graph, the analysis has to be done before this is called.
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

	protected:

		// One node of the flame graph. The children are the indices of the nodes of the storage, so a
		// node stays valid when the storage grows.
		struct FlameNode
		{
			FlameNode(std::pmr::memory_resource* resource)
				: children(resource) {}

			EventID event_id;
			// The sum of the duration of every occurrence of this call path.
			DurationSec total_duration = DurationSec::zero();
			// The time span which the occurrences of the call path cover, it is the union of their ranges, and it is
			// what the required time range of the report is checked against. The span is only valid when at least one
			// occurrence of the path is aggregated.
			Potato::Misc::IndexSpan<DurationSec> time_range;
			bool has_time_range = false;
			std::pmr::vector<std::size_t> children;
		};

		// One kept stack tree: the duration which picks the longest ones and a copy of the events, which are the source
		// of the aggregation. The events of the view which the parser receives only point into the storage of the
		// analysis, which is reused after the stack tree is analyzed, so they are copied here.
		struct StoredView
		{
			StoredView(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: events(resource) {}

			DurationSec duration = DurationSec::zero();
			ThreadID thread_id;
			ThreadSystemID system_thread_id;
			std::optional<std::size_t> frame_count;
			std::pmr::vector<ThreadCPUEvent> events;
		};

		// Aggregate one kept stack tree into the flame graph.
		void AggregateStackTree(ThreadCPUEventView event_scope);

		std::pmr::unsynchronized_pool_resource resource;
		EventSpecManager events_manager;
		// The query system which is built from Config::query, the ids of the events are resolved into
		// it when they are discovered, so the stack tree is never matched by a name.
		EventQueryMapping query_mapping;
		// The storage of the nodes, the node 0 is the synthetic root of the graph.
		std::pmr::vector<FlameNode> nodes;
		std::u8string query_text;
		// The stack trees which are kept, they are the Config::max_view_count longest ones which the query matches.
		std::pmr::vector<StoredView> stored_views;
		std::size_t max_view_count = 10;
	};
}
