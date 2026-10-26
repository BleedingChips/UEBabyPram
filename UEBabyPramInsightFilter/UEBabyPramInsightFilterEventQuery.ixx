module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterEventQuery;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// The query system which is built from the event queries of the analysis, it turns every query
	// into the ids which it matches, so the stack tree is searched by ids instead of by names.
	//
	// One EventQuery is a call chain: every element of the chain is a regular expression, and the
	// regular expression of an element is resolved into the ids of the events whose name it matches.
	// The chain condition is checked as well: the second element of the chain has to be a descendant
	// of the first one, the third one has to be a descendant of the second one, and so on, and the
	// elements are matched in the order of the call, but the elements do not have to be the nearest
	// ancestors or descendants, the events which are not a part of the chain may be between them.
	//
	// The thread condition of a query is resolved the same way, its regular expression is resolved
	// into the ids of the threads whose name it matches, so the parser looks the thread of the stack
	// tree up in the query instead of matching its name again, and a query which carries no thread
	// condition counts every thread. The time ranges of a query filter the occurrences themselves: an
	// occurrence is counted when its whole time range overlaps one of the ranges, the events which only
	// complete the call chain are the context of the occurrence and they are not filtered by the time,
	// and a query which carries no time range counts every occurrence.
	//
	// Every chain element which is marked as statistic ([^Regex^]) is reported with its occurrence of
	// every matched chain, so the same event name is counted independently by two queries. The elements
	// which are only a part of the call chain ((^Regex^)) are never reported, and an element reports an
	// occurrence only once, even when the chain is completed more than once inside the subtree of that
	// occurrence.
	//
	// Note: every element of the chain is resolved into all of the ids which its regular expression
	// matches, so a name which appears in more than one element of the same chain is counted by all of
	// those elements, the query system does not pick the topmost one of them.
	class EventQueryMapping
	{
	public:
		struct Config
		{
			// The queries of the analysis, every one of them is built by ParseEventQuery, they are
			// taken over by the query system.
			std::vector<EventQuery> queries;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One element of the call chain of a query, its regular expression is resolved into the ids of
		// the events whose name it matches.
		struct ChainNode
		{
			ChainNode(std::pmr::memory_resource* resource)
				: event_ids(resource) {}

			EventQuery::ChainElement::Kind kind = EventQuery::ChainElement::Kind::QueryOnly;
			// The regular expression of the element, it is the one which the query is built with.
			std::shared_ptr<re2::RE2> matcher;
			// The ids of the events whose name is fully matched by the regular expression, they are
			// appended when the events are discovered, so the order is the discovery order. The
			// element which is only a part of the call chain keeps them as well, they are what the
			// chain is matched by, and an element whose list stays empty never completes a chain.
			std::pmr::vector<EventID> event_ids;
		};

		// The query system of one EventQuery.
		struct Query
		{
			Query(std::pmr::memory_resource* resource)
				: nodes(resource), thread_ids(resource), time_ranges(resource) {}

			// The call chain, it is never empty.
			std::pmr::vector<ChainNode> nodes;
			// The regular expression of the thread condition, it is empty when the query does not
			// filter the thread, and such a query counts every thread.
			std::shared_ptr<re2::RE2> thread_matcher;
			// The ids of the threads whose name is fully matched by the thread matcher, it is only
			// filled for the query which carries the thread condition.
			std::pmr::vector<ThreadID> thread_ids;
			// The time ranges of the query, it is empty when the query does not filter the time.
			std::pmr::vector<Potato::Misc::IndexSpan<DurationSec>> time_ranges;
		};

		EventQueryMapping(Config config = {});

		// Resolve one discovered event against the chain of every query. It has to be called once per
		// event before the stack tree which carries it is analyzed, an event which carries no name is
		// never resolved. The name of an event spec is wide and the name of the event of the CPU scope
		// logger is UTF-8, the wide name is converted into the scratch storage of the query system.
		void AddEvent(EventID id, std::wstring_view event_name);
		void AddEvent(EventID id, std::u8string_view event_name);
		// Resolve one discovered thread against the thread condition of every query, the query which
		// carries no thread condition ignores it, it counts every thread.
		void AddThread(ThreadID id, std::string_view thread_name);

		std::span<Query const> GetQueries() const { return queries; }

		// Whether any query counts the occurrences which happened on the thread. A query which carries no
		// thread condition counts every thread, so it is only false when every query carries a thread
		// condition which the thread is not matched by, and such a thread carries no event which has to
		// be counted.
		bool IsThreadMatched(ThreadID thread_id) const;

		// One event of the stack tree which completes a query and which a statistic element of the query
		// counts, the event and the element which counts it.
		struct EventMatch
		{
			// The matched event, the view carries its whole subtree and its time range.
			ThreadCPUEventView::EventView event_view;
			// The index of the query which the event belongs to, see GetQueries().
			std::size_t query_index = 0;
			// The index of the chain element of the query which counts the event.
			std::size_t node_index = 0;
		};

		// The visitor of the matches, the match is only valid while the visitor is running, so it must
		// not be stored by the visitor.
		using MatchVisitorT = Potato::TMP::FunctionRef<void(EventMatch const&)>;

		// Visit every event of the stack tree which has to be counted: the event belongs to a statistic
		// element of a query, the thread condition and the time ranges of that query are matched, and the
		// whole call chain of the query is completed with the event as that chain element. The events are
		// visited in the order of the call.
		//
		// The chains of a query are enumerated by a depth first search: the candidate of an element is
		// searched inside the subtree of the element before it, and the next candidate of an element is
		// searched after the whole subtree of the candidate which was visited, so the candidates of one
		// element are never nested into each other and the candidates of one element are pairwise
		// disjoint. That is what keeps one element from reporting the same event twice: an event belongs
		// to at most one candidate of an element, and the elements above it report their occurrence only
		// once (see FindedNodeStack::has_export below).
		// The price of it is that a candidate which is nested inside another candidate of the same element
		// is never tried, so an occurrence which is only reachable through such a candidate (ex: an event
		// which is nested inside another event of the same element) is not reported, the occurrences of
		// an element are counted by its outermost candidates.
		void ForEachMatchedEvent(ThreadCPUEventView event_scope, MatchVisitorT visitor) const;

	protected:

		// Whether the query counts the occurrences which happened on the thread, a query which carries no
		// thread condition counts them all.
		static bool IsThreadMatched(Query const& query, ThreadID thread_id);

		// Whether any time range of the query overlaps the range. It is used both for the whole stack tree
		// (a query whose time ranges do not overlap the tree can not count any occurrence of it, so the
		// search of its chains is skipped) and for every occurrence which is about to be reported (an
		// occurrence whose time range overlaps none of the ranges is not counted). A query which carries no
		// time range is overlapped by every range, so it counts every occurrence.
		static bool IsTimeRangeOverlapped(Query const& query, Potato::Misc::IndexSpan<DurationSec> time_range);

		std::pmr::unsynchronized_pool_resource resource;
		std::pmr::vector<Query> queries;
		// The matcher needs the event name in UTF-8, the scratch buffer avoids an allocation per event.
		std::u8string encoded_event_name;
	};
}
