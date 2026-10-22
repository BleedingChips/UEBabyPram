module;

#include <re2/re2.h>

export module UEBabyPramInsightFilterThreadSearch;

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
	// Search the threads of an insight trace with a re2 matcher of the thread name, and report the
	// name of every matched thread. The same name may be carried by several threads (ex: every audio
	// device creates its own callback thread), those threads are merged into one name, and the count
	// of the threads which carry the name is reported with it.
	//
	// The regex has to match the whole thread name (re2::RE2::FullMatch), use ".*" when only a part
	// of the name is known, ex: ".*Thread". When the regex is empty (Config::matcher is empty or the
	// pattern of the matcher is empty), every thread of the trace is reported.
	//
	// The thread names are discovered from the thread info of the trace, which is reported for every
	// thread without parsing any event of the trace, so no parser flag is required by this parser.
	struct ThreadSearchParser : public Parser
	{
		struct Config
		{
			// The re2 matcher of the thread name, it is compiled from the regex by the creator of the
			// config, ex: std::make_shared<re2::RE2>(std::string_view{ "GameThread" }).
			// An empty matcher, or a matcher whose pattern is empty, matches every thread.
			std::shared_ptr<re2::RE2> matcher;
			std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		};

		// The matched threads which share one thread name.
		struct ThreadRecord
		{
			ThreadRecord(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
				: thread_name(resource), thread_ids(resource) {}

			std::pmr::wstring thread_name;
			// The ids of the threads which carry this name, the threads of the same name are merged
			// into this record.
			std::pmr::vector<ThreadID> thread_ids;
		};

		ThreadSearchParser(Config config = {});

		// The thread info of the trace is reported to this callback for every thread, so the thread
		// is checked against the matcher here, and only the matched thread is stored, merged with the
		// threads which already carry its name.
		virtual void OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name) override;
		// The thread info is analysed even when no flag is required, no event of the trace has to be
		// parsed to know the threads.
		virtual bool IsParserRequired(ParserRequireFlag flag) const override;
		virtual void AllAnalyzeDone() override;
		bool PrintToLog(Potato::Log::LogPrinter& printer) override;

		// Every matched thread name, ordered by the name, the threads which share the name are merged
		// into one record, so the count of the record is the count of the threads of that name.
		std::span<ThreadRecord const> GetThreads() const { return threads; }
		// Count of the discovered threads, both the matched and the unmatched ones are counted.
		std::size_t GetDiscoveredThreadCount() const { return discovered_thread_count; }
		// Whether the empty regex is used, in which case every thread is matched.
		bool IsMatcherEmpty() const { return match_all; }

	protected:

		std::pmr::unsynchronized_pool_resource resource;
		std::pmr::vector<ThreadRecord> threads;
		// The ids of the discovered threads, a thread may be reported more than once, it is only
		// checked and counted the first time.
		std::pmr::unordered_set<ThreadID> discovered_thread_ids;
		std::shared_ptr<re2::RE2> matcher;
		// The empty regex matches every thread, it is decided once instead of per thread.
		bool match_all = false;
		std::size_t discovered_thread_count = 0;
	};
}
