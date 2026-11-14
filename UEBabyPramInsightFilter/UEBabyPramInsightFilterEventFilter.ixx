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
	//   chain [ '&' ( 'Time' | 'ITime' ) '=' [TimePoint, TimePoint]+ ] [ '&' Thread '=' (^Regex^) ]
	// Every chain element is (^Regex^) if the event is only matched as a part of the call chain, [^Regex^] if
	// its timing is also accumulated, %[^Regex^] if every event which it matches is accumulated into one
	// statistic, or % if any event is matched.
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
				// [^Regex^]: the event is matched and its timing is accumulated.
				QueryAndStatistic,
				// %[^Regex^]: the event is matched and its timing is accumulated, and every event which the element
				// matches is accumulated into one statistic, so the matched names share one timing.
				QueryAndStatisticMerged,
			};
			Kind kind = Kind::QueryOnly;

			// The regular expression of the event name, it is compiled from the text between
			// the '^' delimiters and matches the whole event name. It is empty when the kind is
			// Any, because such an element matches every event.
			std::shared_ptr<re2::RE2> matcher;
		};

		// Which time condition the query carries. A query carries at most one of them, because an occurrence which
		// the contained condition selects is a part of an occurrence which the overlap condition selects, so the two
		// are one value with the slices which it is applied to instead of two lists of slices.
		enum class TimeConditionKind : std::uint8_t
		{
			// The query carries no time condition, every occurrence of the chain is counted.
			None,
			// Time: an occurrence is counted when its time range overlaps one of the slices.
			Overlap,
			// ITime: an occurrence is counted when one of the slices contains it or is equal to it, so the whole time
			// range of the occurrence has to be inside that slice.
			Contained,
		};

		// The event call chain, the order is the call order, it is never empty.
		std::vector<ChainElement> chain;
		// The slices of the time condition which the query carries, every [TimePoint, TimePoint] is one of them, and
		// the kind of the condition decides what they mean. The slices are written one after another in the query
		// text, so '&Time=[0s,10s][20s,30s]' carries two of them, and the list is empty when the kind is None.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> time_ranges;
		// The time condition which the slices above are applied with.
		TimeConditionKind time_condition = TimeConditionKind::None;
		// The thread name matcher, empty when the query does not filter the thread.
		std::shared_ptr<re2::RE2> thread_matcher;
	};

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