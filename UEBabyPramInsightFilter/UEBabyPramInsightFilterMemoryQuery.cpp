module;

#include <re2/re2.h>

module UEBabyPramInsightFilterMemoryQuery;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	bool IsMemoryTrackerMatched(EventQuery const& query, std::u8string_view tracker_name)
	{
		// The Tracker condition selects the tracker which produced the snapshot, and a query which carries no Tracker
		// condition selects every tracker. The Thread condition is not read by the parsers of the memory channel, and the
		// command line rejects a memory query which carries it instead of ignoring it.
		if (!query.tracker_matcher)
		{
			return true;
		}

		return IsEventNameMatched(*query.tracker_matcher, tracker_name);
	}

	bool IsMemoryTimePointMatched(EventQuery const& query, DurationSec time)
	{
		// The time point of a snapshot is a single point, so the boundary of a slice is a part of it: the slice covers the
		// points from its beginning to its end. Time and ITime select the same snapshots, because a point is contained by a
		// slice exactly when it overlaps it.
		if (query.time_ranges.empty())
		{
			return true;
		}

		for (auto const& slice : query.time_ranges)
		{
			if (slice.Begin() <= time && time <= slice.End())
			{
				return true;
			}
		}

		return false;
	}

	bool IsMemoryTagNameMatched(EventQuery const& query, std::u8string_view tag_full_name)
	{
		if (query.chain.empty() || tag_full_name.empty())
		{
			return false;
		}

		std::u8string_view remaining = tag_full_name;

		for (std::size_t chain_index = 0; chain_index < query.chain.size(); ++chain_index)
		{
			std::size_t separator = remaining.find(u8'/');
			std::u8string_view segment = separator == std::u8string_view::npos
				? remaining
				: remaining.substr(0, separator);

			auto const& element = query.chain[chain_index];
			if (element.kind != EventQuery::ChainElement::Kind::Any)
			{
				if (!element.matcher || !IsEventNameMatched(*element.matcher, segment))
				{
					return false;
				}
			}

			if (separator == std::u8string_view::npos)
			{
				// The name is exhausted with this segment, and the tag is only selected when this was the last element of the
				// chain: the chain names the tag itself, it does not select the tags below it.
				return chain_index + 1 == query.chain.size();
			}

			remaining = remaining.substr(separator + 1);
		}

		// Every element of the chain matched one segment and the name carries more segments than the chain, so the tag is a
		// child of the tag which the chain names: a chain selects exactly the tag of its last element, and a tag below it is
		// selected by a chain which names it, ex: the element which matches any segment, %, walks one level deeper.
		return false;
	}
}
