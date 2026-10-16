module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterEventSearchParser;

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
	// Search the event specs of an insight trace with a re2 matcher of the event name,
	// and report every definition location (the file path + the line) of the matched events.
	//
	// An event name is not unique, the same name may be defined in several places, so the
	// matched events are grouped by the event name and every location is reported separately.
	struct EventSearchParser : public Parser
	{
		struct Config
		{
			// The re2 matcher of the event name, it is compiled from the regex by the creator
			// of the config, ex: std::make_shared<re2::RE2>(std::string_view{ "FEngineLoop::.*" }).
			// The regex has to match the whole event name (re2::RE2::FullMatch), use ".*" when
			// only a part of the name is known, ex: ".*Tick.*".
			// The parser reports nothing when the matcher is empty or is not valid (re2::RE2::ok).
			std::shared_ptr<re2::RE2> matcher;
			// Max location count which will be kept and reported, 0 for no limit.
			std::size_t max_location_count = 256;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One definition location of a matched event name.
		struct EventLocation
		{
			std::pmr::wstring event_name;
			std::pmr::wstring file;
			std::size_t line = 0;
			// How many event specs share this event name and this location.
			std::size_t event_count = 0;
		};

		EventSearchParser(Config config = {});

		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// Every matched location, ordered by the event name, the file path and the line.
		std::span<EventLocation const> GetEventLocations() const { return event_locations; }
		// Count of the matched event specs, before the events which share the same name and location are merged.
		std::size_t GetMatchedEventCount() const { return matched_event_count; }
		// Count of the scanned event specs.
		std::size_t GetScannedEventCount() const { return scanned_event_count; }
		// Count of the location which is dropped by Config::max_location_count.
		std::size_t GetDroppedLocationCount() const { return dropped_location_count; }
		// The readable error of Config::matcher, empty when the matcher is usable.
		std::wstring_view GetMatcherError() const { return matcher_error; }

	protected:

		bool IsEventNameMatched(std::wstring_view event_name);

		std::pmr::unsynchronized_pool_resource resource;
		EventSpecManager events_manager;
		std::pmr::vector<EventLocation> event_locations;
		std::shared_ptr<re2::RE2> matcher;
		std::wstring matcher_error;
		// The matcher needs the event name in UTF-8, the scratch buffer avoids an allocation per event.
		std::u8string encoded_event_name;
		std::size_t max_location_count = 256;
		std::size_t scanned_event_count = 0;
		std::size_t matched_event_count = 0;
		std::size_t dropped_location_count = 0;
	};
}
