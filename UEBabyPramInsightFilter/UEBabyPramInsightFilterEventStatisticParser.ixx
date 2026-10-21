module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterEventStatisticParser;

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
	// Collect the timing of every event whose name is fully matched by Config::matcher.
	//
	// Every single occurrence of an event is found in the stack tree of the CPU event, and is
	// accumulated into the statistic of its event name:
	//   - the occurrence count, the total duration and the total exclude time (the duration without
	//     the time spent in the child events),
	//   - the median and the standard deviation of the duration of the single occurrence, which are
	//     built from the stored occurrences in AllAnalyzeDone(),
	//   - the time ranges of the top N (Config::top_count) longest occurrences.
	// One event name may be carried by several event ids, the ids which carry a name are merged into
	// the statistic of that name, so every name is counted separately.
	struct EventStatisticParser : public Parser
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
			// How many of the longest occurrences of every event name are kept as a time range,
			// 0 for none.
			std::size_t top_count = 5;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One of the longest occurrences of an event name, the time range of the occurrence and its
		// exclude time are kept, so that the occurrence can be reported.
		struct TopEventOccurrence
		{
			Potato::Misc::IndexSpan<DurationSec> time_range;
			// The duration of the occurrence without the time spent in the child events.
			DurationSec exclude_time = DurationSec::zero();
		};

		// The statistics of one event name.
		// The same event name may be carried by several event ids (it may be declared in several
		// places, and the event of the CPU scope logger is a separate id as well), so the ids which
		// carry the name are kept with the statistic. The name itself is not stored, it is taken
		// from the events manager with the first id of the statistic.
		// Every occurrence of one of the ids is accumulated here, and the time ranges of the longest
		// occurrences are kept, so that the occurrences which cost the most time can be reported.
		struct EventStatistic
		{
			EventStatistic(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_ids(resource), durations(resource), top_occurrences(resource) {}

			// The ids of the events which carry this name, the occurrences of all of them are merged
			// into this statistic. The ids are the ones which the parser receives and the ones
			// which are carried by the stack tree of the CPU event, the id of the event of the CPU
			// scope logger is inverted, the events manager resolves it.
			std::pmr::vector<EventID> event_ids;
			// The count of the occurrences of this name.
			std::size_t count = 0;
			// One duration per occurrence, the durations are kept to build the median and the
			// standard deviation of the duration of the single occurrence in AllAnalyzeDone().
			std::pmr::vector<DurationSec> durations;
			// The total duration of the occurrences, which is the sum of the duration of every
			// single occurrence.
			DurationSec total_duration = DurationSec::zero();
			// The total duration of the occurrences without the time spent in the child events.
			DurationSec total_exclude_time = DurationSec::zero();
			// The median of the duration of the single occurrence, which is built in AllAnalyzeDone().
			DurationSec median_duration = DurationSec::zero();
			// The standard deviation of the duration of the single occurrence, which is the square
			// root of the variance of the durations, it is built in AllAnalyzeDone().
			DurationSec duration_standard_deviation = DurationSec::zero();
			// The top N (Config::top_count) longest occurrences, each one keeps its time range and its
			// exclude time, the shortest one is kicked out when the list is full and a longer
			// occurrence is found, the order is unspecified.
			std::pmr::vector<TopEventOccurrence> top_occurrences;
		};

		EventStatisticParser(Config config = {});

		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// Every event statistic, one per matched event name, the order is unspecified.
		std::span<EventStatistic const> GetEventStatistics() const { return event_statistics; }

	protected:

		// Add a matched event to the statistic of its name, returns the index of the statistic in
		// event_statistics. The event has to be stored in the events manager before this is called,
		// the name is resolved from the id, and the id is appended to the ids of the statistic which
		// already carries that name. A new statistic is only created when there is no statistic of
		// that name yet, so the statistics are grouped by the name as soon as the events are found.
		std::size_t AddEventStatistic(EventID id);
		// Find the next occurrence of the event which the statistic belongs to, the search starts
		// from the offset of the stack tree view. The search is kept apart from the walk of the
		// stack tree, the rule of which event is accepted as an occurrence will grow later.
		// Returns an empty view when there is no more occurrence in the rest of the view.
		ThreadCPUEventView::EventView FindStatisticEvent(ThreadCPUEventView event_scope, EventStatistic const& statistic, std::size_t offset) const;
		// Record one occurrence of the event of the statistic: the occurrence count, the total
		// duration and the total exclude time are accumulated, the sample of the occurrence is
		// stored for the median and the standard deviation, and the time range of the occurrence is
		// kept when it ranks in the top N of the statistic. It is kept apart from the walk of the
		// stack tree, more statistics of the occurrence will grow here later.
		void RecordEventOccurrence(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, EventStatistic& statistic);

		std::pmr::unsynchronized_pool_resource resource;
		EventSpecManager events_manager;
		std::shared_ptr<re2::RE2> matcher;
		// The matcher needs the event name in UTF-8, the scratch buffer avoids an allocation per call.
		std::u8string encoded_event_name;
		
		std::pmr::vector<EventStatistic> event_statistics;

		std::size_t top_count = 5;
	};
}
