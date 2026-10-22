module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterEventSearch;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace UEBabyPram::InsightParser;
}

export namespace UEBabyPram::InsightFilter
{
	// Search the events of an insight trace with a re2 matcher of the event name,
	// and report every definition location (the file path + the line) of the matched events.
	//
	// An event name is not unique, the same name may be defined in several places, so the
	// matched events are grouped by the event name and every location is reported separately.
	//
	// Every discovered event is stored in the events manager, which also dedups it: the same
	// event id may be reported once per scope instance, but it is only stored once. Only when the
	// event is stored for the first time its name is checked against the matcher, and only the id
	// of the matched event is recorded, so the name is never matched twice for the same event and
	// the report is built from the stored events by filtering them with the recorded ids.
	//
	// The report is classified by the event name: one regex may match several event names, and one
	// event name may be carried by several event ids and defined in several places, so every event
	// name is reported as its own statistic with its own event count and its own locations.
	//
	// Both kinds of the events are searched:
	//   - the event specs of the CPU profiler, which carry the definition location,
	//   - the events of the "Cpu" scope logger, which only carry the name.
	// The scope event has no file and no line, so it is reported with an empty file and line 0,
	// and its name is the only information which it contributes to the report.
	struct EventSearchParser : public Parser
	{
		struct Config
		{
			// The re2 matcher of the event name, it is compiled from the regex by the creator
			// of the config, ex: std::make_shared<re2::RE2>(std::string_view{ "FEngineLoop::.*" }).
			// The regex has to match the whole event name (re2::RE2::FullMatch), use ".*" when
			// only a part of the name is known, ex: ".*Tick.*".
			// The matcher has to be valid (re2::RE2::ok), the error of an invalid matcher is not
			// reported by the parser, the creator has to check it and report it before the parser
			// is created, which is what the command line does.
			std::shared_ptr<re2::RE2> matcher;
			// Max location count which will be kept and reported, 0 for no limit.
			std::size_t max_location_count = 256;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One definition location of a matched event name, the name itself is kept by the
		// EventNameStatistic which owns the location.
		struct EventLocation
		{
			// The file path and the line of the definition, both are empty/0 when the location is
			// not known, which is always the case for the event of the CPU scope logger.
			std::pmr::wstring file;
			std::size_t line = 0;
			// How many event specs share this event name and this location.
			std::size_t event_count = 0;
		};

		// The statistics of one matched event name.
		// One regex may match several event names, and one event name may be carried by several
		// event ids and defined in several places, so every event name is classified and counted
		// separately, and every event name keeps its own locations.
		struct EventNameStatistic
		{
			EventNameStatistic(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_name(resource), locations(resource) {}

			std::pmr::wstring event_name;
			// The locations of this name, ordered by the file path and the line.
			std::pmr::vector<EventLocation> locations;
			// How many matched event ids carry this name.
			std::size_t event_count = 0;
		};

		EventSearchParser(Config config = {});

		// Every event is stored in the events manager, the name is only checked against the matcher
		// when the event is stored for the first time, and only the id of the matched event is
		// recorded. The event which carries no name is never matched.
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		// The event of the CPU scope logger carries no file and no line, its name is searched and
		// reported with an empty file and line 0. The same event id is reported for every scope
		// instance, but it is only stored and matched once, the same way as the event spec.
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// Every matched event name, ordered by the name, each name keeps its own locations and its
		// own count, so the names which are matched by the same regex are never merged together.
		std::span<EventNameStatistic const> GetEventNameStatistics() const { return event_name_statistics; }
		// Count of the matched events, which is built in AllAnalyzeDone() from the events manager,
		// it is the sum of the event count of every event name and is not affected by
		// Config::max_location_count.
		std::size_t GetMatchedEventCount() const { return matched_event_count; }
		// Count of the stored events, which is accumulated when an event is stored for the first
		// time, both the event specs and the events of the CPU scope logger are counted.
		std::size_t GetScannedEventCount() const { return scanned_event_count; }
		// Count of the location which is dropped by Config::max_location_count, the locations of a
		// name are dropped together when the limit is reached in the middle of that name.
		std::size_t GetDroppedLocationCount() const { return dropped_location_count; }

	protected:

		// Record the id of the matched event, it is called only when the event is stored for the
		// first time. The id which the manager stores may differ from the given one (the event of
		// the CPU scope logger is stored with its real id), so it is read back from the manager.
		void RecordMatchedEvent(EventID id);

		std::pmr::unsynchronized_pool_resource resource;
		EventSpecManager events_manager;
		// One statistic per matched event name, ordered by the name.
		std::pmr::vector<EventNameStatistic> event_name_statistics;
		// The ids of the matched events, it is used in AllAnalyzeDone() to pick the matched events
		// out of the events manager.
		std::pmr::unordered_set<EventID> matched_event_ids;
		std::shared_ptr<re2::RE2> matcher;
		// The matcher needs the event name in UTF-8, the scratch buffer avoids an allocation per event.
		std::u8string encoded_event_name;
		std::size_t max_location_count = 256;
		std::size_t scanned_event_count = 0;
		std::size_t matched_event_count = 0;
		std::size_t dropped_location_count = 0;
	};
}
