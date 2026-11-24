module;

#include <cassert>
#include <re2/re2.h>

export module UEBabyPramInsightFilterEventFilter;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThread;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// The event query which is built from the query text, the grammar is described by
	// GetFilterEbnf():
	//   chain [ '&' ( 'Time' | 'ITime' ) '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
	//         [ '&' 'Tracker' '=' (^Regex^) ]
	// Every chain element is (^Regex^) if the event is only matched as a part of the call chain, [^Regex^] if
	// its timing is also accumulated, %[^Regex^] if every event which it matches is accumulated into one
	// statistic, %% if any event is matched and its timing is accumulated, or % if any event is matched. The
	// pattern '.*' is the same as matching any event, so (^.*^) is read as %, [^.*^] is read as %%, and
	// %[^.*^] is read as the merged statistic of every event; none of the three carries a regular expression.
	// Every regular expression of the query, the ones of the chain
	// elements and the ones of the Thread and the Tracker conditions, matches the whole name, a partial match is
	// not accepted.
	//
	// Time selects the occurrences whose time range overlaps one of its slices, ITime selects the occurrences which
	// one of its slices contains or equals. The parsers of the memory channel read the time point of a snapshot, so
	// the two conditions select the same snapshots there.
	struct EventQuery
	{
		// One element of the event call chain.
		struct ChainElement
		{
			enum class Kind : std::uint8_t
			{
				// (^Regex^): the event is only matched as a part of the call chain.
				QueryOnly,
				// %: any event is matched as a part of the call chain, it carries no regular
				// expression and it is never reported.
				Any,
				// %% (or [^.*^]): any event is matched and its timing is accumulated, the
				// element carries no regular expression and every event keeps its own statistic.
				AnyAndStatics,
				// %[^.*^]: any event is matched and its timing is accumulated, the element carries
				// no regular expression and every event which it matches shares one statistic.
				AnyAndStatisticMerged,
				// [^Regex^]: the event is matched and its timing is accumulated.
				QueryAndStatistic,
				// %[^Regex^]: the event is matched and its timing is accumulated, and every event which the element
				// matches is accumulated into one statistic, so the matched names share one timing.
				QueryAndStatisticMerged,
			};
			Kind kind = Kind::QueryOnly;

			// The regular expression of the event name, it is compiled from the text between
			// the '^' delimiters and matches the whole event name. It is empty when the kind is
			// Any, AnyAndStatics or AnyAndStatisticMerged, because such an element matches every event.
			std::shared_ptr<re2::RE2> matcher;
		};

		// Which time condition the query carries, a query carries at most one of them because the slices are applied with
		// one condition instead of two lists of slices.
		//
		// The two conditions differ for the occurrences of the call stacks: an occurrence may cross the boundary of a
		// slice, and Time counts it while ITime drops it. The parsers of the memory channel read the time point of a
		// snapshot instead of a range, so Time and ITime select the same snapshots there.
		enum class TimeConditionKind : std::uint8_t
		{
			// The query carries no time condition, every occurrence is counted.
			None,
			// Time: an occurrence is counted when its time range overlaps one of the slices, so an occurrence which
			// crosses the boundary of a slice is counted with its whole time.
			Overlap,
			// ITime: an occurrence is counted when one of the slices contains it or is equal to it, so the whole time
			// range of the occurrence has to be inside that slice.
			Contained,
		};

		// The event call chain, the order is the call order, it is never empty.
		std::vector<ChainElement> chain;
		// The slices of the time condition which the query carries, every [TimePoint, TimePoint] is one of them, the
		// kind above decides what they select, and the list is empty when the kind is None.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> time_ranges;
		// The time condition which the slices above are applied with.
		TimeConditionKind time_condition = TimeConditionKind::None;
		// The thread name matcher of the Thread condition, it matches the whole name of the thread. It is empty when
		// the query does not filter the thread.
		std::shared_ptr<re2::RE2> thread_matcher;
		// The tracker name matcher of the Tracker condition, it matches the whole name of the memory tracker. Only the
		// parsers of the memory channel read it, because a tracker is a spec of the LLM channel, and it is empty when
		// the query does not filter the tracker.
		std::shared_ptr<re2::RE2> tracker_matcher;
	};

	// Whether the element matches every event without a regular expression, which is the Any, the
	// AnyAndStatics and the AnyAndStatisticMerged kind: the Any kind only completes the call chain, the
	// other two accumulate the timing of every event which they match.
	constexpr bool IsAnyChainElementKind(EventQuery::ChainElement::Kind kind)
	{
		return kind == EventQuery::ChainElement::Kind::Any
			|| kind == EventQuery::ChainElement::Kind::AnyAndStatics
			|| kind == EventQuery::ChainElement::Kind::AnyAndStatisticMerged;
	}

	// Whether the element accumulates the timing of the events which it matches, which is the
	// QueryAndStatistic, the QueryAndStatisticMerged, the AnyAndStatics and the AnyAndStatisticMerged kind.
	// The merged kinds share one statistic between every event which they match, the other two keep one
	// statistic per event name.
	constexpr bool IsStatisticChainElementKind(EventQuery::ChainElement::Kind kind)
	{
		return kind == EventQuery::ChainElement::Kind::QueryAndStatistic
			|| kind == EventQuery::ChainElement::Kind::QueryAndStatisticMerged
			|| kind == EventQuery::ChainElement::Kind::AnyAndStatics
			|| kind == EventQuery::ChainElement::Kind::AnyAndStatisticMerged;
	}

	// The conditions which a query carries. Every parser accepts the conditions which it reads and rejects the others,
	// so a query which the parser would only partly apply is reported as an error instead of being accepted with a
	// condition which silently does nothing.
	enum class QueryConditionFlag : std::uint8_t
	{
		Time = 1 << 0,
		Thread = 1 << 1,
		Tracker = 1 << 2,
	};

	constexpr QueryConditionFlag operator|(QueryConditionFlag left, QueryConditionFlag right)
	{
		return static_cast<QueryConditionFlag>(
			static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right)
		);
	}

	// Whether the flags carry the condition.
	constexpr bool HasQueryCondition(QueryConditionFlag flags, QueryConditionFlag condition)
	{
		return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(condition)) != 0;
	}

	// Whether every condition of the second argument is a part of the first argument.
	constexpr bool IsQueryConditionAccepted(QueryConditionFlag accepted, QueryConditionFlag condition)
	{
		return (static_cast<std::uint8_t>(accepted) & static_cast<std::uint8_t>(condition))
			== static_cast<std::uint8_t>(condition);
	}

	// The conditions which the query carries.
	QueryConditionFlag GetQueryCondition(EventQuery const& query);

	// The error of a query which carries a condition which the parser does not read, empty when every condition of the
	// query is accepted. The message is human readable UTF-8 and it names the conditions which are not read, so the
	// caller reports it as it is.
	std::u8string CheckQueryCondition(EventQuery const& query, QueryConditionFlag accepted);

	// Build the event query from the text, the text is not owned by the query and the
	// regular expressions are compiled into the query. Returns std::u8string instead of the query
	// when the text is not acceptable, which means that it does not follow the grammar of
	// GetFilterEbnf(), or that one of its regular expressions is not accepted by re2. The message is
	// human readable UTF-8 and it names the part of the text which is not accepted, so the caller is
	// able to report it as it is. When the query is returned instead, its chain is never empty.
	std::variant<EventQuery, std::u8string> ParseEventQuery(std::u8string_view query);

	// The EBNF table of the event query grammar, it is built once and kept for the whole
	// process, so the temporary resources of the table are only kept in the process as well.
	Potato::EBNF::Ebnf const& GetFilterEbnf();
}