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
	// Every single occurrence of an event is a sample, which carries the duration of the occurrence
	// and its exclude time (the duration without the time spent in the child events). All samples
	// which share the same event name are merged, sorted by the duration of the single occurrence
	// in ascending order, and reported with:
	//   - the occurrence count, the mean duration and the mean exclude time of all samples,
	//   - the median and the standard deviation of the duration of the single occurrence.
	//
	// The top N (Config::top_count, default is 5) longest single occurrences are reported with their
	// duration, exclude time and the call chain which leads to the occurrence.
	struct EventStatisticParser : public Parser
	{
		struct Config
		{
			// The re2 matcher of the event name, it is compiled from the regex by the creator
			// of the config, ex: std::make_shared<re2::RE2>(std::string_view{ "FEngineLoop::.*" }).
			// The regex has to match the whole event name (re2::RE2::FullMatch), use ".*" when
			// only a part of the name is known, ex: ".*Tick.*".
			// The parser reports nothing when the matcher is empty or is not valid (re2::RE2::ok).
			std::shared_ptr<re2::RE2> matcher;
			// How many of the longest single occurrences are reported with the call chain, 0 for none.
			std::size_t top_count = 5;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// One occurrence of an event.
		struct EventSample
		{
			DurationSec duration = DurationSec::zero();
			// The duration of the occurrence without the time spent in the child events.
			DurationSec exclude_time = DurationSec::zero();
		};

		// The statistics of one event name, the samples are sorted by the duration in ascending order.
		struct EventStatistic
		{
			EventStatistic(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_name(resource), samples(resource) {}

			std::pmr::wstring event_name;
			std::pmr::vector<EventSample> samples;
			// The count of the samples, which is the occurrence count of the event.
			std::size_t count = 0;
			DurationSec mean_duration = DurationSec::zero();
			DurationSec mean_exclude_time = DurationSec::zero();
			// The median of the duration of the single occurrence.
			DurationSec median_duration = DurationSec::zero();
			// The standard deviation of the duration of the single occurrence, it is the square
			// root of the variance of the durations.
			DurationSec duration_standard_deviation = DurationSec::zero();
		};

		// The longest single occurrence of an event, with the call chain which leads to it.
		struct TopEventRecord
		{
			TopEventRecord(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: event_name(resource), thread_name(resource), call_chain(resource) {}

			std::pmr::wstring event_name;
			ThreadID thread_id;
			std::pmr::wstring thread_name;
			DurationSec duration = DurationSec::zero();
			DurationSec exclude_time = DurationSec::zero();
			// The names of the enclosing events and of the event itself, from the outermost to the
			// innermost, so the last element is always the event itself.
			std::pmr::vector<std::pmr::wstring> call_chain;
		};

		EventStatisticParser(Config config = {});

		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		virtual void OnCPUEventDiscovered(EventID id, std::wstring_view event_name, std::wstring_view file_name, std::size_t file_line) override;
		virtual void OnCPUScopeEventDiscovered(EventID id, std::u8string_view event_name) override;
		virtual void OnCPUStackTree(ThreadCPUEventView event_scope) override;
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// Every event statistic, ordered by the mean duration in ascending order.
		std::span<EventStatistic const> GetEventStatistics() const { return event_statistics; }
		// The longest single occurrences, ordered by the duration in descending order.
		std::span<TopEventRecord const> GetTopEventRecords() const { return top_records; }
		// The readable error of Config::matcher, empty when the matcher is usable.
		std::wstring_view GetMatcherError() const { return matcher_error; }

	protected:

		// Create the statistic of a matched event spec, returns its index in event_statistics.
		std::size_t AddEventStatistic(EventID id, std::wstring_view event_name);
		// The name of the event, "<Unknow>" when the spec of the event is not discovered.
		std::pmr::wstring CopyEventName(EventID id);
		std::wstring_view GetThreadName(ThreadID thread_id);
		// Collect one sample of a matched event, and keep it in the top list when it is long enough.
		void RecordSample(ThreadCPUEventView event_scope, ThreadCPUEventView::EventView event_view, std::size_t statistic_index);
		void MergeEventStatistics();
		void BuildEventStatistic(EventStatistic& statistic);

		std::pmr::unsynchronized_pool_resource resource;
		EventSpecManager events_manager;
		std::shared_ptr<re2::RE2> matcher;
		std::wstring matcher_error;
		// The matcher needs the event name in UTF-8, the scratch buffer avoids an allocation per call.
		std::u8string encoded_event_name;
		// The matched event id -> the index of its statistic in event_statistics.
		std::pmr::unordered_map<EventID, std::size_t> matched_event_index;
		// One statistic per matched event id, the statistics of the event specs which share the same
		// name are merged in AllAnalyzeDone() and are ordered by the mean duration afterwards.
		std::pmr::vector<EventStatistic> event_statistics;
		std::pmr::vector<TopEventRecord> top_records;
		std::pmr::unordered_map<std::size_t, std::pmr::wstring> thread_names;
		std::size_t top_count = 5;
	};
}
