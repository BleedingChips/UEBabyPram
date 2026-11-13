module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterEventQuery;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	namespace
	{
		// Whether the element of the chain collects the timing of the events which it matches, both of the statistic
		// kinds do it, and the merged one shares one statistic between all the events which it matches.
		bool IsStatisticKind(EventQuery::ChainElement::Kind kind)
		{
			return kind == EventQuery::ChainElement::Kind::QueryAndStatistic
				|| kind == EventQuery::ChainElement::Kind::QueryAndStatisticMerged;
		}
	}

	EventQueryMapping::EventQueryMapping(Config in_config)
		: resource(in_config.resource)
		, queries(&resource)
	{
		for (auto& query : in_config.queries)
		{
			// The query is turned into the query system of the mapping, the regular expressions of its
			// chain are kept, they are the place where the ids of the events are collected.
			auto& target = queries.emplace_back(&resource);
			target.thread_matcher = std::move(query.thread_matcher);
			target.time_ranges.assign(query.time_ranges.begin(), query.time_ranges.end());

			for (auto& element : query.chain)
			{
				auto& node = target.nodes.emplace_back(&resource);
				node.kind = element.kind;
				node.matcher = std::move(element.matcher);
				assert(node.matcher || node.kind == EventQuery::ChainElement::Kind::Any);
			}
		}
	}

	auto EventQueryMapping::AddEvent(EventID id, std::u8string_view event_name, ExportVisitorT visitor) -> AddEventResult
	{
		AddEventResult result;

		if (!id || event_name.empty())
		{
			return result;
		}

		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto& query = queries[query_index];
			for (std::size_t node_index = 0; node_index < query.nodes.size(); ++node_index)
			{
				auto& node = query.nodes[node_index];
				if (!node.matcher)
				{
					continue;
				}
				// The whole name has to be matched, the same as the name of a thread.
				if (IsEventNameMatched(*node.matcher, event_name))
				{
					// The element resolves every id only once: an id which the element already carries is not added
					// again, and it is neither reported as matched nor exported again, so a name which is discovered
					// more than once (ex: the event of the CPU scope logger) is not exported more than once.
					if (std::find(node.event_ids.begin(), node.event_ids.end(), id) != node.event_ids.end())
					{
						result.is_marked = true;
						continue;
					}

					node.event_ids.push_back(id);
					result.is_matched = true;

					// Only the element whose timing is collected exports the event, the elements which are only a part
					// of the call chain resolve the chain without exporting anything.
					if (IsStatisticKind(node.kind))
					{
						result.is_exported = true;
						if (visitor)
						{
							visitor(id, query_index, node_index);
						}
					}
				}
			}
		}

		return result;
	}

	auto EventQueryMapping::AddEvent(EventID id, std::wstring_view event_name, ExportVisitorT visitor) -> AddEventResult
	{
		// The matcher needs the name in UTF-8, the event spec carries it in wide characters.
		encoded_event_name.clear();
		Potato::Encode::UnicodeEncoder<wchar_t, char8_t>::EncodeTo(
			event_name,
			std::back_inserter(encoded_event_name)
		);
		return AddEvent(id, std::u8string_view{ encoded_event_name }, visitor);
	}

	void EventQueryMapping::AddThread(ThreadID id, ThreadSystemID system_id, std::string_view thread_name)
	{
		if (!id)
		{
			return;
		}

		for (auto& query : queries)
		{
			// The query which carries no thread condition counts every thread, so it never keeps the ids
			// of the threads, IsThreadMatched() answers it without them.
			if (!query.thread_matcher)
			{
				continue;
			}

			// The whole name has to be matched, the same as the name of an event.
			if (re2::RE2::FullMatch(thread_name, *query.thread_matcher))
			{
				// The thread is kept with both of its ids, the stack tree carries the thread id and the events which
				// are not carried by the stack tree carry the system thread id.
				query.thread_ids.push_back(id);
				query.thread_system_ids.push_back(system_id);
			}
		}
	}

	bool EventQueryMapping::ForEachMatchedEvent(ThreadCPUEventView event_scope, MatchVisitorT visitor) const
	{
		if (!visitor || queries.empty())
		{
			return false;
		}

		// The whole stack tree is the range which the time conditions are checked against first, a query
		// whose time ranges do not overlap the tree can not count any occurrence of it.
		auto tree_time_range = event_scope.GetTimeRange();
		if (!tree_time_range)
		{
			return false;
		}

		// Whether the whole call chain of a query is matched inside the tree, the chain of a query which only
		// carries query elements reports no occurrence, so the visitor alone can not tell whether it is
		// matched.
		bool is_chain_matched = false;

		struct FindedNodeStack
		{
			ThreadCPUEventView::EventView event_view;
			std::size_t node_index = 0;
			EventQuery::ChainElement::Kind kind = EventQuery::ChainElement::Kind::QueryOnly;
			// Whether an occurrence of this element has been reported for the chain which it is a part of,
			// it keeps the element above from being reported again when the chain is completed more than
			// once inside the subtree of that occurrence.
			bool has_export = false;
		};

		// The chain which is being matched, every entry is one matched element of the chain, and the
		// entries of a query are contiguous from the first element of the chain.
		std::vector<FindedNodeStack> finded_stack;

		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto const& query = queries[query_index];
			assert(!query.nodes.empty());

			// A query which carries no thread condition counts every thread, and a query which carries no
			// time range counts every occurrence, so the two conditions only filter the queries which
			// carry them.
			if (!IsThreadMatched(query, event_scope.thread_id))
			{
				continue;
			}

			if (!IsTimeRangeOverlapped(query, *tree_time_range))
			{
				continue;
			}

			// An element whose regular expression matched no event never completes the chain, so the chain
			// of the query can not be matched in this tree at all. The element which matches any event
			// never carries an id, it is always resolvable.
			bool chain_resolvable = true;
			for (auto const& node : query.nodes)
			{
				if (node.kind != EventQuery::ChainElement::Kind::Any && node.event_ids.empty())
				{
					chain_resolvable = false;
					break;
				}
			}

			if (!chain_resolvable)
			{
				continue;
			}

			finded_stack.clear();

			// The first candidate of the first element of the chain, its next candidate is searched after
			// the whole subtree of this one, which is done by the backtrack below.
			auto first_candidate = event_scope.FindChildEvent(query.nodes[0].GetEventIDs());
			if (first_candidate)
			{
				finded_stack.emplace_back(FindedNodeStack{ first_candidate, 0, query.nodes[0].kind, false });
			}

			while (!finded_stack.empty())
			{
				auto& top = finded_stack.back();
				assert(top.event_view);

				if (top.node_index + 1 < query.nodes.size())
				{
					// The candidate of the next element is searched inside the subtree of the element before
					// it, so the events which are not a part of the chain may be between them.
					auto const& next_node = query.nodes[top.node_index + 1];
					auto child_candidate = event_scope.FindChildEvent(next_node.GetEventIDs(), top.event_view);
					if (child_candidate)
					{
						finded_stack.emplace_back(FindedNodeStack{
							child_candidate,
							top.node_index + 1,
							next_node.kind,
							false
							}
						);
						continue;
					}
				}
				else {
					// The whole chain is matched, so the chain of the query is reported as matched, and every
					// statistic element of it reports its occurrence: the occurrence is filtered by the time ranges
					// of the query, an occurrence whose time range overlaps none of them is not counted, and an
					// element which already reported an occurrence of this chain is skipped.
					is_chain_matched = true;
					for (auto& stack_ite : finded_stack)
					{
						if (
							IsStatisticKind(stack_ite.kind)
							&& !stack_ite.has_export
							)
						{
							stack_ite.has_export = true;
							if (IsTimeRangeOverlapped(query, stack_ite.event_view.time_range))
							{
								visitor(
									EventMatch{
										stack_ite.event_view,
										query_index,
										stack_ite.node_index
									}
								);
							}
						}
					}
				}

				// Backtrack: the chain is enumerated again with the next candidate of the deepest element,
				// and the elements below it are searched again from that candidate, so every combination of
				// the chain is visited. The next candidate is searched after the whole subtree of the
				// element which is left, inside the subtree of the element which holds it, or in the whole
				// stack tree for the first element of the chain, so the candidates of one element are
				// pairwise disjoint and an event is never a candidate of an element twice. The next
				// candidate of the first element is what keeps the search going after the subtree of a
				// first element is done, without it the query would only visit the first chain of the tree.
				while (!finded_stack.empty())
				{
					auto left_element = finded_stack.back();
					finded_stack.pop_back();

					std::size_t search_end = finded_stack.empty()
						? event_scope.view.size()
						: finded_stack.back().event_view.index_range.End();

					auto next_candidate = event_scope.FindNextEvent(
						query.nodes[left_element.node_index].GetEventIDs(),
						{ left_element.event_view.index_range.End(), search_end }
					);

					if (next_candidate)
					{
						finded_stack.emplace_back(FindedNodeStack{
							next_candidate,
							left_element.node_index,
							query.nodes[left_element.node_index].kind,
							false
							}
						);
						break;
					}
				}
			}
		}

		return is_chain_matched;
	}

	bool EventQueryMapping::IsChainMatched(ThreadCPUEventView event_scope, Query const& query)
	{
		assert(!query.nodes.empty());

		// The chain is walked the same way as ForEachMatchedEvent() walks it, the candidate of an element is searched
		// inside the subtree of the element before it, and the next candidate of an element is searched after the whole
		// subtree of the candidate which was visited. The walk stops as soon as the whole chain is completed, because
		// the only answer which is asked for is whether the chain is completed at all, so the rest of the combinations
		// of the chain is not visited and no occurrence is exported.
		struct FindedNode
		{
			ThreadCPUEventView::EventView event_view;
			std::size_t node_index = 0;
		};

		std::vector<FindedNode> finded_stack;
		finded_stack.reserve(query.nodes.size());

		auto first_candidate = event_scope.FindChildEvent(query.nodes[0].GetEventIDs());
		if (!first_candidate)
		{
			return false;
		}

		finded_stack.push_back(FindedNode{ first_candidate, 0 });

		while (!finded_stack.empty())
		{
			std::size_t top_node_index = finded_stack.back().node_index;

			if (top_node_index + 1 < query.nodes.size())
			{
				// The candidate of the next element is searched inside the subtree of the element before it, so the
				// events which are not a part of the chain may be between them.
				auto const& next_node = query.nodes[top_node_index + 1];
				auto child_candidate = event_scope.FindChildEvent(next_node.GetEventIDs(), finded_stack.back().event_view);
				if (child_candidate)
				{
					finded_stack.push_back(FindedNode{ child_candidate, top_node_index + 1 });
					continue;
				}
			}
			else {
				// The whole chain is completed inside the tree, which is the answer of the walk: the tree satisfies the
				// query, and the walk does not have to visit the other combinations of the chain.
				return true;
			}

			// Backtrack to the next candidate of the deepest element which still carries one, the same as
			// ForEachMatchedEvent() does, so the candidates of one element are pairwise disjoint and no event is tried
			// twice as the same element.
			while (!finded_stack.empty())
			{
				auto left_element = finded_stack.back();
				finded_stack.pop_back();

				std::size_t search_end = finded_stack.empty()
					? event_scope.view.size()
					: finded_stack.back().event_view.index_range.End();

				auto next_candidate = event_scope.FindNextEvent(
					query.nodes[left_element.node_index].GetEventIDs(),
					{ left_element.event_view.index_range.End(), search_end }
				);

				if (next_candidate)
				{
					finded_stack.push_back(FindedNode{ next_candidate, left_element.node_index });
					break;
				}
			}
		}

		return false;
	}

	std::optional<std::size_t> EventQueryMapping::FindMatchedQuery(ThreadCPUEventView event_scope) const
	{
		if (queries.empty())
		{
			return std::nullopt;
		}

		// The whole stack tree is the range which the time conditions are checked against first, a query whose time
		// ranges do not overlap the tree can not match any occurrence of it.
		auto tree_time_range = event_scope.GetTimeRange();
		if (!tree_time_range)
		{
			return std::nullopt;
		}

		for (std::size_t query_index = 0; query_index < queries.size(); ++query_index)
		{
			auto const& query = queries[query_index];
			assert(!query.nodes.empty());

			// A query which carries no thread condition counts every thread, and a query which carries no time range
			// counts every occurrence, so the two conditions only filter the queries which carry them.
			if (!IsThreadMatched(query, event_scope.thread_id))
			{
				continue;
			}

			if (!IsTimeRangeOverlapped(query, *tree_time_range))
			{
				continue;
			}

			// An element whose regular expression matched no event never completes the chain, so the chain of the query
			// can not be matched in this tree at all. The element which matches any event never carries an id, it is
			// always resolvable.
			bool chain_resolvable = true;
			for (auto const& node : query.nodes)
			{
				if (node.kind != EventQuery::ChainElement::Kind::Any && node.event_ids.empty())
				{
					chain_resolvable = false;
					break;
				}
			}

			if (!chain_resolvable)
			{
				continue;
			}

			if (IsChainMatched(event_scope, query))
			{
				return query_index;
			}
		}

		return std::nullopt;
	}

	bool EventQueryMapping::IsMatched(ThreadCPUEventView event_scope) const
	{
		return FindMatchedQuery(event_scope).has_value();
	}

	bool EventQueryMapping::IsThreadMatched(ThreadID thread_id) const
	{
		for (auto const& query : queries)
		{
			if (IsThreadMatched(query, thread_id))
			{
				return true;
			}
		}

		return false;
	}

	bool EventQueryMapping::IsThreadMatched(ThreadSystemID thread_system_id) const
	{
		for (auto const& query : queries)
		{
			if (IsThreadMatched(query, thread_system_id))
			{
				return true;
			}
		}

		return false;
	}

	bool EventQueryMapping::IsThreadMatched(Query const& query, ThreadID thread_id)
	{
		// A query which carries no thread condition counts every thread, so it keeps no thread id.
		if (!query.thread_matcher)
		{
			return true;
		}

		return std::find(query.thread_ids.begin(), query.thread_ids.end(), thread_id) != query.thread_ids.end();
	}

	bool EventQueryMapping::IsThreadMatched(Query const& query, ThreadSystemID thread_system_id)
	{
		// A query which carries no thread condition counts every thread, so it keeps no system thread id.
		if (!query.thread_matcher)
		{
			return true;
		}

		return std::find(query.thread_system_ids.begin(), query.thread_system_ids.end(), thread_system_id) != query.thread_system_ids.end();
	}

	bool EventQueryMapping::IsTimeRangeOverlapped(Query const& query, Potato::Misc::IndexSpan<DurationSec> time_range)
	{
		// A query which carries no time range is overlapped by every range.
		if (query.time_ranges.empty())
		{
			return true;
		}

		// The range is checked against every range of the query, the adjacent ranges are not overlapped, so
		// a range which only touches the end of a query range is not counted.
		for (auto const& range : query.time_ranges)
		{
			if (range.IsOverlap(time_range))
			{
				return true;
			}
		}

		return false;
	}
}