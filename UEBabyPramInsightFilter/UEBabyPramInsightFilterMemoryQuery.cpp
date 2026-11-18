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
		if (!query.thread_matcher)
		{
			return true;
		}

		return IsEventNameMatched(*query.thread_matcher, tracker_name);
	}

	bool IsMemoryTimePointMatched(EventQuery const& query, DurationSec time)
	{
		switch (query.time_condition)
		{
		case EventQuery::TimeConditionKind::None:
			return true;
		case EventQuery::TimeConditionKind::Overlap:
			// The time point of a snapshot is a single point, so the boundary of a slice is a part of it: the slice covers
			// the points from its beginning to its end.
			for (auto const& slice : query.time_ranges)
			{
				if (slice.Begin() <= time && time <= slice.End())
				{
					return true;
				}
			}
			return false;
		case EventQuery::TimeConditionKind::Contained:
			// The points which are strictly inside a slice, which is a part of the points which the Overlap condition selects
			// and may be empty.
			for (auto const& slice : query.time_ranges)
			{
				if (slice.Begin() < time && time < slice.End())
				{
					return true;
				}
			}
			return false;
		}

		return true;
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
