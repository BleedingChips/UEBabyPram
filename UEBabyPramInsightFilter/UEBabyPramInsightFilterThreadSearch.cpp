module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterThreadSearch;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace InsightParser;

	ThreadSearchParser::ThreadSearchParser(Config in_config)
		: resource(in_config.resource)
		, threads(&resource)
		, discovered_thread_ids(&resource)
		, matcher(std::move(in_config.matcher))
		// The empty regex matches every thread, the matcher is not needed at all in that case.
		, match_all(!matcher || matcher->pattern().empty())
	{
		// The matcher is compiled and checked by the creator of the config, the parser only reports
		// the thread which the matcher matches.
		assert(match_all || matcher->ok());
	}

	void ThreadSearchParser::OnThreadDiscovered(ThreadID thread_id, ThreadSystemID thread_system_id, std::string_view thread_name)
	{
		// A thread may be reported more than once, it is only checked and counted the first time.
		if (!discovered_thread_ids.insert(thread_id).second)
		{
			return;
		}

		++discovered_thread_count;

		// The empty regex matches every thread, otherwise the name has to be fully matched.
		if (!match_all && !re2::RE2::FullMatch(thread_name, *matcher))
		{
			return;
		}

		// The name is needed for the comparison and for a new record, it is built once.
		std::pmr::wstring name{ &resource };
		Potato::Encode::UnicodeEncoder<char, wchar_t>::EncodeTo(
			thread_name,
			std::back_inserter(name)
		);

		// The threads which share the name are merged, the id is appended to the record of the name,
		// and a new record is only created when there is no record of that name yet.
		for (auto& record : threads)
		{
			if (record.thread_name == name)
			{
				record.thread_ids.push_back(thread_id);
				return;
			}
		}

		auto& record = threads.emplace_back(&resource);
		record.thread_name = std::move(name);
		record.thread_ids.push_back(thread_id);
	}

	bool ThreadSearchParser::IsParserRequired(ParserRequireFlag flag) const
	{
		// No event of the trace has to be parsed, the thread info is reported to OnThreadDiscovered
		// for every thread even when no route is registered.
		return false;
	}

	void ThreadSearchParser::AllAnalyzeDone()
	{
		Parser::AllAnalyzeDone();

		// Report the thread names in a stable order, the threads of the same name are already merged.
		std::sort(threads.begin(), threads.end(), [](ThreadRecord const& left, ThreadRecord const& right) {
			return left.thread_name < right.thread_name;
			});
	}

	bool ThreadSearchParser::PrintToLog(Potato::Log::LogPrinter& printer)
	{
		using InsightFilter::OutputLog;

		OutputLog<L"ThreadSearchParser Output:">(printer);

		if (match_all)
		{
			OutputLog<L"    Thread Regex <empty>, Discovered Thread <{}>, Matched Name <{}>">(
				printer,
				discovered_thread_count,
				threads.size()
			);
		}
		else {
			OutputLog<L"    Thread Regex <{}>, Discovered Thread <{}>, Matched Name <{}>">(
				printer,
				std::string_view{ matcher->pattern() },
				discovered_thread_count,
				threads.size()
			);
		}

		for (auto const& thread : threads)
		{
			OutputLog<L"    Thread:<{:}>, Count:<{}>">(
				printer,
				std::wstring_view{ thread.thread_name },
				thread.thread_ids.size()
			);
		}

		return true;
	}
}
