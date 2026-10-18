module;

#include <re2/re2.h>

module UEBabyPramInsightFilterEventSearchParser;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	EventSearchParser::EventSearchParser(Config in_config)
		: resource(in_config.resource)
		, events_manager(EventSpecManager::Config{ 10000, 1000, true, &resource })
		, event_locations(&resource)
		, scanned_event_ids(&resource)
		, matcher(std::move(in_config.matcher))
		, matcher_error(DescribeMatcherError(matcher))
		, max_location_count(in_config.max_location_count)
	{
	}

	bool EventSearchParser::IsNewScannedEvent(EventID id)
	{
		if (scanned_event_ids.insert(id).second)
		{
			++scanned_event_count;
			return true;
		}
		return false;
	}

	void EventSearchParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// An event without a name is never reported, every other event has to match the matcher.
		if (!matcher_error.empty() || !IsNewScannedEvent(id) || event_name.empty() || !IsEventNameMatched(*matcher, event_name, encoded_event_name))
		{
			return;
		}

		// Only the matched event is stored, AllAnalyzeDone() takes the report from the events manager directly.
		events_manager.AddEvent(id, event_name, file_name, file_line);
	}

	bool EventSearchParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// Only the event spec, which carries the name and the definition location of every event,
		// is required. The event batch, which carries the timing data, does not have to be parsed.
		return flag == ParserRequireFlag::EVENT_SPEC;
	}

	void EventSearchParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		event_locations.clear();
		matched_event_count = 0;
		dropped_location_count = 0;

		if (!matcher_error.empty())
		{
			return;
		}

		std::pmr::vector<EventLocation> matched_locations{ &resource };

		// Every event in the events manager already matched the matcher when it was discovered,
		// so the matched locations are taken from it without any further filtering.
		events_manager.ForEachEvent([&](EventSpecManager::EventSpecView const& event_spec) {
			++matched_event_count;
			matched_locations.push_back(EventLocation{
				std::pmr::wstring{ event_spec.name_view, &resource },
				std::pmr::wstring{ event_spec.file_view, &resource },
				event_spec.line,
				1
				});
			});

		// The same event name may be defined in several places,
		// sort the locations by the name, the file path and the line,
		// then merge the events which share the same name and the same location.
		std::sort(
			matched_locations.begin(),
			matched_locations.end(),
			[](EventLocation const& left, EventLocation const& right) {
				if (auto order = left.event_name <=> right.event_name; order != 0)
				{
					return order < 0;
				}
				if (auto order = left.file <=> right.file; order != 0)
				{
					return order < 0;
				}
				return left.line < right.line;
			}
		);

		for (auto& ite : matched_locations)
		{
			if (!event_locations.empty())
			{
				auto& last_location = event_locations.back();
				if (
					last_location.event_name == ite.event_name
					&& last_location.file == ite.file
					&& last_location.line == ite.line
					)
				{
					last_location.event_count += ite.event_count;
					continue;
				}
			}
			event_locations.push_back(std::move(ite));
		}

		if (max_location_count != 0 && event_locations.size() > max_location_count)
		{
			dropped_location_count = event_locations.size() - max_location_count;
			event_locations.erase(
				event_locations.begin() + max_location_count,
				event_locations.end()
			);
		}
	}

	bool EventSearchParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventSearchParser Output:">(printer);

		if (!matcher_error.empty())
		{
			OutputLog<L"    Unsupport Event Matcher : <{}>">(printer, matcher_error);
			return false;
		}

		OutputLog<L"    Event Regex <{}>, Scanned Event <{}>, Matched Event <{}>, Matched Location <{}>">(
			printer,
			std::string_view{ matcher->pattern() },
			scanned_event_count,
			matched_event_count,
			event_locations.size()
		);

		std::size_t index = 0;
		while (index < event_locations.size())
		{
			std::size_t next_index = index + 1;
			while (
				next_index < event_locations.size()
				&& event_locations[next_index].event_name == event_locations[index].event_name
				)
			{
				++next_index;
			}

			std::size_t event_count = 0;
			for (std::size_t sub_index = index; sub_index < next_index; ++sub_index)
			{
				event_count += event_locations[sub_index].event_count;
			}

			OutputLog<L"    Name:<{:}>, Location:<{:}>, Event:<{:}>:">(
				printer,
				std::wstring_view{ event_locations[index].event_name },
				next_index - index,
				event_count
			);

			for (std::size_t sub_index = index; sub_index < next_index; ++sub_index)
			{
				auto& location = event_locations[sub_index];
				std::wstring_view file_view = location.file.empty()
					? std::wstring_view{ L"<Unknow>" }
					: std::wstring_view{ location.file };

				OutputLog<L"        [{}] File:<{:}>, Line:<{:}>">(
					printer,
					sub_index - index,
					file_view,
					location.line
				);
			}

			index = next_index;
		}

		if (dropped_location_count > 0)
		{
			OutputLog<L"    <{:}> more location is not reported, use a bigger max_location_count to see all of them">(
				printer,
				dropped_location_count
			);
		}

		return true;
	}
}
