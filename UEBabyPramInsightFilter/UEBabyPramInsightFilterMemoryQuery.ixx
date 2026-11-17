module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterMemoryQuery;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

export namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;

	// The parts of an EventQuery which the parsers of the memory channel read, and which are the same for every one of
	// them: the data of the memory channel is shaped differently from the call stacks, so three conditions of the query are
	// read differently, and they are answered here instead of in every parser which walks a snapshot.
	//
	//   - The Thread condition selects the memory trackers: a snapshot is produced by one tracker, so the condition is
	//     matched against the name of the tracker, and a query which carries no Thread condition selects every tracker.
	//   - The call chain is matched against the parent chain of the memory tag: a tag is named by the segments of its
	//     ancestors, ex: A/B/C, so every element of the chain is matched against one segment from the root, and the tags
	//     which the query selects are the tag of the last element of the chain and every tag below it, ex: the chain
	//     (^A^) -> (^B^) selects A/B, A/B/C, A/B/C/D and so on. The element which matches any event, %, matches any segment,
	//     and a tag whose name carries fewer segments than the chain is never selected.
	//   - The time condition is applied to the time point of a snapshot instead of the time range of an occurrence: the time
	//     of a snapshot is a single point, so Time selects the snapshots which are inside one of its slices or on the
	//     boundary of one, and ITime selects the snapshots which are strictly inside one of its slices, which is a part of
	//     the slices and may select no snapshot at all.

	// Whether the tracker of a snapshot is selected by the query.
	bool IsMemoryTrackerMatched(EventQuery const& query, std::u8string_view tracker_name);

	// Whether the time point of a snapshot is selected by the time condition of the query.
	bool IsMemoryTimePointMatched(EventQuery const& query, DurationSec time);

	// Whether the name of a memory tag is selected by the chain of the query.
	bool IsMemoryTagNameMatched(EventQuery const& query, std::u8string_view tag_full_name);
}
