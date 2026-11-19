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
	//   - The Tracker condition selects the memory trackers: a snapshot is produced by one tracker, so the condition is
	//     matched against the whole name of the tracker, and a query which carries no Tracker condition selects every
	//     tracker. The Thread condition is a condition of the call stacks, so a memory query which carries it is rejected by
	//     the command line instead of being accepted with a condition which does nothing.
	//   - The call chain names one memory tag by its parent chain: a tag is named by the segments of its ancestors, ex:
	//     A/B/C, so every element of the chain is matched against one segment from the root, and a tag is selected only when
	//     the chain covers its whole name, ex: the chain (^A^) -> (^B^) selects A/B and nothing below it. The element which
	//     matches any event, % , matches any segment, so a chain which carries % at its end walks one level deeper, and a
	//     tag whose name carries a different count of segments than the chain is never selected.
	//   - The time condition is applied to the time point of a snapshot instead of the time range of an occurrence: the time
	//     of a snapshot is a single point, so the condition selects the snapshots which are inside one of its slices or on
	//     the boundary of one. Time and ITime are one condition, both select the snapshots which equal or are contained by
	//     a slice.

	// Whether the tracker of a snapshot is selected by the query.
	bool IsMemoryTrackerMatched(EventQuery const& query, std::u8string_view tracker_name);

	// Whether the time point of a snapshot is selected by the time condition of the query.
	bool IsMemoryTimePointMatched(EventQuery const& query, DurationSec time);

	// Whether the name of a memory tag is selected by the chain of the query.
	bool IsMemoryTagNameMatched(EventQuery const& query, std::u8string_view tag_full_name);

	// The width of the value of a memory tag, taken from the id of a tag instead of the int64 of the platform types, which
	// this module does not include: the value of a tag is the int64 of the trace, so the two are the same type.
	using MemoryValue = decltype(MemoryTagId{}.id);

	// One sample of a memory tag: the time point of a snapshot and the value which the snapshot sampled for the tag. It is
	// the unit which the parsers of the memory channel keep, so they share it instead of declaring one each.
	struct MemoryValueSample
	{
		DurationSec time = DurationSec::zero();
		MemoryValue value = 0;
	};
}
