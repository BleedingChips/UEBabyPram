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
	//   chain [ '&' Time '=' [TimePoint, TimePoint]+ ] [ '&' Thread '=' (^Regex^) ]
	// Every chain element is (^Regex^) if the event is only matched as a part of the call
	// chain, or [^Regex^] if its timing is also accumulated.
	struct EventQuery
	{
		// One element of the event call chain.
		struct ChainElement
		{
			enum class Kind : std::uint8_t
			{
				// (^Regex^): the event is only matched as a part of the call chain.
				QueryOnly,
				// [^Regex^]: the event is matched and its timing is accumulated.
				QueryAndStatistic,
			};

			Kind kind = Kind::QueryOnly;
			// The regular expression of the event name, it is compiled from the text between
			// the '^' delimiters and matches the whole event name.
			std::shared_ptr<re2::RE2> matcher;
		};

		// The event call chain, the order is the call order, it is never empty.
		std::vector<ChainElement> chain;
		// The time ranges of the query, every [TimePoint, TimePoint] is one range.
		std::vector<Potato::Misc::IndexSpan<DurationSec>> time_ranges;
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
