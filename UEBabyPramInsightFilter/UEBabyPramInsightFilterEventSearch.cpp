module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterEventSearch;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	EventSearchParser::EventSearchParser(Config in_config)
		: resource(in_config.resource)
		, events_manager(EventSpecManager::Config{ true, &resource })
		, event_name_statistics(&resource)
		, matched_event_ids(&resource)
		, matcher(std::move(in_config.matcher))
		, max_location_count(in_config.max_location_count)
	{
		// The matcher is compiled and checked by the creator of the config, an invalid matcher has
		// to be reported there, the parser never reports the error of the matcher.
		assert(matcher && matcher->ok());
	}

	void EventSearchParser::OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line)
	{
		// Every event is stored, the return value of AddEvent tells whether this is the first time
		// that this event spec is stored, the name only has to be checked once per spec.
		if (!events_manager.AddEvent(id, event_name, file_name, file_line))
		{
			return;
		}

		++scanned_event_count;

		// An event without a name is never reported, every other event has to match the matcher.
		// Only the id of the matched event is recorded, AllAnalyzeDone() filters the stored events
		// with it, so the name does not have to be matched again.
		if (!event_name.empty() && IsEventNameMatched(*matcher, event_name, encoded_event_name))
		{
			RecordMatchedEvent(id);
		}
	}

	void EventSearchParser::OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name)
	{
		// The event of the CPU scope logger carries no file and no line, it is stored without them.
		// The same id is reported once per scope instance, but AddEvent only stores it once, so the
		// name is only checked once per event id. The name is already UTF-8, which is what the
		// matcher needs, so it is matched in place without any conversion.
		if (!events_manager.AddEvent(id, event_name, {}, 0))
		{
			return;
		}

		++scanned_event_count;

		if (!event_name.empty() && IsEventNameMatched(*matcher, event_name))
		{
			RecordMatchedEvent(id);
		}
	}

	void EventSearchParser::RecordMatchedEvent(EventID id)
	{
		// The id may be inverted when it is stored (the event of the CPU scope logger is stored with
		// its real id), so the id is taken from the manager to make it match the stored event spec.
		if (auto event_spec = events_manager.GetEventSpec(id); event_spec)
		{
			matched_event_ids.insert(event_spec.id);
		}
	}

	bool EventSearchParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// The event spec carries the name and the definition location of every event, and the
		// metadata carries the name of the event of the CPU scope logger, which carries no location.
		// The event batch, which carries the timing data, does not have to be parsed.
		return flag == ParserRequireFlag::EVENT_SPEC
			|| flag == ParserRequireFlag::META_DATA;
	}

	void EventSearchParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		event_name_statistics.clear();
		matched_event_count = 0;
		dropped_location_count = 0;

		// One location per matched event id, the ids which share the same name and the same location
		// are merged below. The name is only kept here, it is moved into the statistic which owns
		// the location, so a name is never copied into every location of it.
		struct DiscoveredLocation
		{
			std::pmr::wstring event_name;
			std::pmr::wstring file;
			std::size_t line = 0;
			std::size_t event_count = 1;
		};

		std::pmr::vector<DiscoveredLocation> discovered_locations{ &resource };

		// Every event is stored in the events manager, but only the id of the matched events is
		// recorded there, so the locations are taken from it with a single lookup per event.
		events_manager.ForEachEvent([&](EventSpecManager::EventSpecView const& event_spec) {
			if (!matched_event_ids.contains(event_spec.id))
			{
				return;
			}
			++matched_event_count;

			// The strings of the event spec are UTF-8, the report keeps them in wide characters.
			std::pmr::wstring event_name{ &resource };
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(
				event_spec.name_view,
				std::back_inserter(event_name)
			);

			std::pmr::wstring file{ &resource };
			Potato::Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(
				event_spec.file_view,
				std::back_inserter(file)
			);

			discovered_locations.push_back(DiscoveredLocation{
				std::move(event_name),
				std::move(file),
				event_spec.line
				});
			});

		// The same event name may be defined in several places, sort the locations by the name,
		// the file path and the line, so that the locations of one name are next to each other and
		// the locations of one name which share the same place are adjacent.
		std::sort(
			discovered_locations.begin(),
			discovered_locations.end(),
			[](DiscoveredLocation const& left, DiscoveredLocation const& right) {
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

		// Classify the locations by the event name, every name gets its own statistic, and the
		// event ids which share the same name and the same location are merged into one location.
		for (auto& location : discovered_locations)
		{
			if (event_name_statistics.empty() || event_name_statistics.back().event_name != location.event_name)
			{
				auto& statistic = event_name_statistics.emplace_back(&resource);
				statistic.event_name = std::move(location.event_name);
			}

			auto& statistic = event_name_statistics.back();
			statistic.event_count += location.event_count;

			if (!statistic.locations.empty())
			{
				auto& last_location = statistic.locations.back();
				if (last_location.file == location.file && last_location.line == location.line)
				{
					last_location.event_count += location.event_count;
					continue;
				}
			}

			statistic.locations.push_back(EventLocation{
				std::move(location.file),
				location.line,
				location.event_count
				});
		}

		if (max_location_count == 0)
		{
			return;
		}

		// Keep the locations in the reported order (the names are ordered by the name, the locations
		// of a name by the file and the line), and count the ones which do not fit the limit.
		std::size_t kept_name_count = 0;
		std::size_t kept_location_count = 0;
		for (auto& statistic : event_name_statistics)
		{
			if (kept_location_count >= max_location_count)
			{
				break;
			}

			std::size_t keep_count = std::min(statistic.locations.size(), max_location_count - kept_location_count);
			if (keep_count < statistic.locations.size())
			{
				// Only a part of the locations of this name fits, drop the rest of them and rebuild
				// the event count from the kept ones.
				dropped_location_count += statistic.locations.size() - keep_count;
				statistic.locations.resize(keep_count);
				statistic.event_count = 0;
				for (auto const& location : statistic.locations)
				{
					statistic.event_count += location.event_count;
				}
			}

			kept_location_count += keep_count;
			++kept_name_count;
		}

		// The names whose locations do not fit at all are not reported.
		for (std::size_t index = kept_name_count; index < event_name_statistics.size(); ++index)
		{
			dropped_location_count += event_name_statistics[index].locations.size();
		}
		event_name_statistics.resize(kept_name_count);
	}

	bool EventSearchParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"EventSearchParser Output:">(printer);

		// The matcher is checked by the creator of the config, the parser never reports its error.
		OutputLog<L"    Event Regex <{}>, Scanned Event <{}>, Matched Event <{}>, Matched Name <{}>">(
			printer,
			std::string_view{ matcher->pattern() },
			scanned_event_count,
			matched_event_count,
			event_name_statistics.size()
		);

		// Every matched event name is reported with its own count and its own locations.
		for (auto& statistic : event_name_statistics)
		{
			OutputLog<L"    Name:<{:}>, Location:<{:}>, Event:<{:}>">(
				printer,
				std::wstring_view{ statistic.event_name },
				statistic.locations.size(),
				statistic.event_count
			);

			for (std::size_t location_index = 0; location_index < statistic.locations.size(); ++location_index)
			{
				auto& location = statistic.locations[location_index];
				std::wstring_view file_view = location.file.empty()
					? std::wstring_view{ L"<Unknow>" }
					: std::wstring_view{ location.file };

				OutputLog<L"        [{}] File:<{:}>, Line:<{:}>">(
					printer,
					location_index,
					file_view,
					location.line
				);
			}
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
