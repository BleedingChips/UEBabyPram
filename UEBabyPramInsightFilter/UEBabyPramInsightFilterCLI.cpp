module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterCLI;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThread;
import UEBabyPramInsightFilterEventFilter;
import UEBabyPramInsightFilterEventSearch;
import UEBabyPramInsightFilterEventStatistic;
import UEBabyPramInsightFilterEventDetail;
import UEBabyPramInsightFilterFlameGraph;
import UEBabyPramInsightFilterThreadSearch;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace Potato;

	namespace
	{
		void PrintGeneralHelp()
		{
			// The help text is longer than the limit of the compile time string template argument (8192 characters),
			// so it is printed in parts: the first part carries the log header and the parts which follow only
			// continue the text, the newline which a part ends with is the one which the log appends by itself.
			Log::Log<CommandLineLogCategory, Log::LogLevel::Display, LR"HELP(
UEBabyPramInsightFilter, report the GameThread timing of an unreal insight .utrace file,
search the events of the trace by a regular expression of the event name, report the
timing statistic of every event whose name is matched by a regular expression, or search
the threads of the trace by a regular expression of the thread name.

Usage:
  UEBabyPramInsightFilter -f <path of the .utrace file> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -r <regex> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -es <query> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -ed <query> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -ef <query> [options]
  UEBabyPramInsightFilter -f <path of the .utrace file> -ts <regex> [options]

Options:
  -f, --file <path>
      Path of the unreal insight .utrace file which should be analyzed. Required.

  -o, --output <path>
      Path of the file which the report is written into, the file is created and its previous content is
      emptied. When it is not given, the report is written into the standard output.

  -r, --regex <regex>
      Regular expression of the event name, in the re2 (RE2) syntax.
      The expression has to match the whole event name,
      ex: -r "FEngineLoop::.*" or -r ".*Tick.*".
      When it is set, every event whose name is matched is reported with the file path and
      the line where it is defined, otherwise the GameThread timing analysis is reported.
      The file path is the source file path at the time the package which produced the trace
      was built, it points to the build machine, so convert it to the local working directory
      path by yourself.
      The same regex may match several event names, the matched events are grouped by the
      event name, every name is counted separately.
      The same event name may be defined in several places, so every location is reported.
      The event of the CPU scope logger carries no location, it is reported with
      an unknown file and line 0.
      Narrow the filter as much as possible: a filter which is too wide reports a very large
      amount of information, which is slow to print and hard to read.
      -r, -es, -ed, -ef and -ts can not be used at the same time.
)HELP">();

			Log::LogTo<Log::DefaultLogFormatter, LR"HELP(  -es, --event_stat <query>
      Query of the events whose timing should be collected, the grammar is:
        chain [ '&' 'Time' '=' [TimePoint, TimePoint]+ ] [ '&' 'Thread' '=' (^Regex^) ]
        chain     := elem ( '->' elem )*
        elem      := (^Regex^)    the event is only matched as a part of the call chain
                  |  [^Regex^]    the event is matched and its timing is collected
                  |  (%)          any event, only matched as a part of the call chain
        TimePoint := Integer 'm' Decimal 's' | Decimal 's' | Integer 's' | Integer 'm' Integer 's'
      Ex: -es "(^FEngineLoop::Tick^)->[^.*TickActor.*^]"
          -es "[^UWorld::Tick^]&Time=[0s,10s]&Thread=(^.*GameThread.*^)"
          -es "(%)->[^UWorld::Tick^]"
      Every regex is the re2 syntax and it has to match the whole event name. The element which
      matches any event, (%), is much more efficient than a regular expression which matches every
      name, ex: (^.*^): the element carries no expression at all, so no name is ever matched against
      one, while the expression has to be tested against the name of every event.
      The chain is matched on the call stack: every element has to be a descendant of the previous
      one and the elements are matched in the order of the call, but the events which are not a part
      of the chain may be between them. Only the elements which are surrounded by '[' and ']' are
      collected, one statistic per event name, and the same name is only collected by the topmost of
      them, so one occurrence is never counted twice by one query.
      The Time and the Thread conditions only filter the collected events, the events which complete
      the call chain are not filtered by them. A collected event is selected by the Time condition
      when the time range which the event occupies overlaps one of the ranges of the query: an event
      which is entirely before a range, or entirely after it, or which only touches its end, is not
      selected. The thread name has to match the whole thread name.
      -es can be given more than once, every query keeps its own statistics, so the same event name
      may be reported several times, once per query which collects it.
      Every occurrence is grouped into the game frame which carries its beginning (the stack trees of
      the game thread which begin with FEngineLoop::Tick or Frame are the frames), and the frames are
      merged by the count of the occurrences which one frame carries: every count reports how many frames
      carry it, the mean of the total time which the event took inside those frames, and the frame of that
      count in which the event took the most time.
      When the count of the occurrences of an event is greater than the top count (-tx), the range from
      the shortest duration of the single occurrence to the longest one is split into top count buckets,
      and every bucket is reported with its range, the count of the occurrences which fell into it and
      the share of them over the whole count.
      Narrow the filter as much as possible: a filter which is too wide reports a very large
      amount of information, which is slow to print and hard to read.
      -r, -es, -ed, -ef and -ts can not be used at the same time.
)HELP">(*Log::GetSTDOutPrinter());

			Log::LogTo<Log::DefaultLogFormatter, LR"HELP(  -ed, --event_detail <query>
      Query of the stack trees which should be saved and reported, the grammar is the same as -es.
      Every stack tree whose thread, time and call chain are matched by the query is copied, the
      -mvc longest ones are kept, and the copies are reported when the whole trace is analyzed. A
      stack tree is only kept while the kept ones are fewer than -mvc, or when it is longer than the
      shortest of them, which is dropped for it, so the count is the count of the longest calls, not
      the count of the calls which are matched first.
      Narrow the filter as much as possible: a filter which is too wide reports a very large
      amount of information, which is slow to print and hard to read.
      -r, -es, -ed, -ef and -ts can not be used at the same time.

  -ef, --event_flame_graph <query>
      Query of the stack trees which should be aggregated into a flame graph, the grammar is the same
      as -es. The call paths of every matched stack tree are merged, and the time of every call path
      is reported with its percentage of the whole matched time. The -mvc longest stack trees are
      aggregated, the same as -ed. Only the occurrences which are inside the time ranges of the query are
      aggregated and reported: an occurrence which is entirely out of the ranges is not a part of the flame
      graph, and the occurrence which crosses the boundary of a range counts with its whole time, the time of
      an occurrence is never cut to the ranges.
      Narrow the filter as much as possible: a filter which is too wide reports a very large
      amount of information, which is slow to print and hard to read.
      -r, -es, -ed, -ef and -ts can not be used at the same time.

  -ts, --thread_search <regex>
      Regular expression of the thread name, in the re2 (RE2) syntax, the same syntax as -r.
      The expression has to match the whole thread name, ex: -ts ".*Thread" or -ts "GameThread".
      When it is set, every thread whose name is matched is reported with the thread id and
      the system thread id.
      An empty regex (ex: -ts "") matches every thread of the trace.
      -r, -es, -ed, -ef and -ts can not be used at the same time.
)HELP">(*Log::GetSTDOutPrinter());

			Log::LogTo<Log::DefaultLogFormatter, LR"HELP(  -mlc, --max_location_count <count>
      How many matched event locations should be reported with -r, 0 for no limit, default is 256.

  -tx, --top_count <count>
      How many of the longest occurrences should be reported with -es, default is 5.

  -mvc, --max_view_count <count>
      How many of the longest stack trees should be saved and reported with -ed, and aggregated
      into the flame graph with -ef, default is 10. The stack tree which is kept is dropped for a
      longer one when the count is reached, so the kept ones are the longest calls which the query
      matches.

  -mrf, --max_record_frame <count>
      How many of the longest game frames should be recorded and reported, default is 10.

  -metc, --max_exclude_time_count <count>
      How many of the longest exclude time events should be kept in every top list, default is 30.

  -sr, --sampling_ratio <ratio>
      Sampling ratio of the analyzed time range, a value in [0.0, 1.0], default is 1.0, which
      analyzes the whole time range. The beginning and the end of the .utrace are the time when the
      trace is saved, which is triggered at the start and at the end of the capture, so the frames
      there take the saving into the timing and their data is not accurate; a ratio below 1.0 only
      analyzes the middle of the time range and leaves both ends out, ex: 0.8 analyzes the middle
      80% of the range.

  -h, --help
      Print this help.
)HELP">(*Log::GetSTDOutPrinter());
		}
	}

	int HandleCommand(int argc, char* argv[], CommandLineSetting& setting)
	{
		GameThreadParser::Config config;
		EventSearchParser::Config search_config;
		EventStatisticParser::Config statistic_config;
		EventDetailParser::Config detail_config;
		EventFlameGraphParser::Config flame_config;
		ThreadSearchParser::Config thread_config;
		bool search_event = false;
		bool statistic_event = false;
		bool detail_event = false;
		bool flame_event = false;
		bool search_thread = false;
		std::u8string path_buffer;

		for (std::size_t i = 1; i < argc; ++i)
		{
			std::string_view argv_string = argv[i];

			if (argv_string == "-h" || argv_string == "--help")
			{
				PrintGeneralHelp();
				return 1;
			}
			else if (argv_string == "-f" || argv_string == "--file")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });
					std::filesystem::path sub_argv = path_buffer;

					if (std::filesystem::exists(sub_argv) && std::filesystem::is_regular_file(sub_argv))
					{
						setting.insight_path = std::move(sub_argv);
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-f --file <path> : <{}> is not an acceptable file, use -h for detailed help">(sub_argv.generic_wstring());
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -f --file, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-o" || argv_string == "--output")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });
					setting.output_path = std::filesystem::path{ path_buffer };
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -o --output, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-r" || argv_string == "--regex")
			{
				if (i + 1 < argc)
				{
					std::string_view pattern = argv[i + 1];
					re2::RE2::Options options;
					// The error is reported below, re2 should not print it a second time.
					options.set_log_errors(false);
					auto matcher = std::make_shared<re2::RE2>(pattern, options);
					if (matcher->ok())
					{
						search_config.matcher = std::move(matcher);
						search_event = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-r --regex <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -r --regex, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mlc" || argv_string == "--max_location_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						search_config.max_location_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mlc --max_location_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mlc --max_location_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-es" || argv_string == "--event_stat")
			{
				if (i + 1 < argc)
				{
					// The query is built from the argument, and the error of an unacceptable query is
					// reported here, so the parser only receives the queries which are ready to be
					// analyzed.
					std::u8string query_text;
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ query_text });
					auto query_result = ParseEventQuery(query_text);
					if (auto error = std::get_if<std::u8string>(&query_result); error != nullptr)
					{
						std::wstring message;
						Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(*error, std::back_inserter(message));
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-es --event_stat <query> : <{}>, use -h for detailed help">(
							message
						);
						return -1;
					}

					// The queries are accumulated, every one of them keeps its own statistics.
					statistic_config.queries.push_back(std::move(std::get<EventQuery>(query_result)));
					statistic_event = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -es --event_stat, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-ed" || argv_string == "--event_detail")
			{
				if (i + 1 < argc)
				{
					// The query is built from the argument, and the error of an unacceptable query is
					// reported here, so the parser only receives a query which is ready to be analyzed.
					std::u8string query_text;
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ query_text });
					auto query_result = ParseEventQuery(query_text);
					if (auto error = std::get_if<std::u8string>(&query_result); error != nullptr)
					{
						std::wstring message;
						Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(*error, std::back_inserter(message));
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ed --event_detail <query> : <{}>, use -h for detailed help">(
							message
						);
						return -1;
					}

					// The detail parser keeps the stack trees which the query matches, only one query
					// is accepted by it, and the text is reported with the saved views.
					detail_config.query = std::move(std::get<EventQuery>(query_result));
					detail_config.query_text = std::move(query_text);
					detail_event = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -ed --event_detail, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-ef" || argv_string == "--event_flame_graph")
			{
				if (i + 1 < argc)
				{
					// The query is built from the argument, and the error of an unacceptable query is
					// reported here, so the parser only receives a query which is ready to be analyzed.
					std::u8string query_text;
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ query_text });
					auto query_result = ParseEventQuery(query_text);
					if (auto error = std::get_if<std::u8string>(&query_result); error != nullptr)
					{
						std::wstring message;
						Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(*error, std::back_inserter(message));
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ef --event_flame_graph <query> : <{}>, use -h for detailed help">(
							message
						);
						return -1;
					}

					// The flame graph parser aggregates the stack trees which the query matches, only
					// one query is accepted by it, and the text is reported with the graph.
					flame_config.query = std::move(std::get<EventQuery>(query_result));
					flame_config.query_text = std::move(query_text);
					flame_event = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -ef --event_flame_graph, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-ts" || argv_string == "--thread_search")
			{
				if (i + 1 < argc)
				{
					std::string_view pattern = argv[i + 1];
					re2::RE2::Options options;
					// The error is reported below, re2 should not print it a second time.
					options.set_log_errors(false);
					auto matcher = std::make_shared<re2::RE2>(pattern, options);
					if (matcher->ok())
					{
						// An empty pattern is accepted, the parser matches every thread with it.
						thread_config.matcher = std::move(matcher);
						search_thread = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ts --thread_search <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -ts --thread_search, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-tx" || argv_string == "--top_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						statistic_config.top_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-tx --top_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -tx --top_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mvc" || argv_string == "--max_view_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						detail_config.max_view_count = value;
						flame_config.max_view_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mvc --max_view_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mvc --max_view_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mrf" || argv_string == "--max_record_frame")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						config.max_record_frame = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mrf --max_record_frame <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mrf --max_record_frame, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-metc" || argv_string == "--max_exclude_time_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						config.max_exclude_time_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-metc --max_exclude_time_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -metc --max_exclude_time_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-sr" || argv_string == "--sampling_ratio")
			{
				if (i + 1 < argc)
				{
					double value = 0.0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value) && value >= 0.0 && value <= 1.0)
					{
						config.sampling_ratio = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-sr --sampling_ratio <ratio> : <{}> is not a valid ratio in [0.0, 1.0], use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -sr --sampling_ratio, use -h for detailed help">();
					return -1;
				}
			}
			else
			{
				Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command <{}>, use -h to see the available commands">(argv_string);
				return -1;
			}
		}

		if (statistic_event && search_event)
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-r --regex and -es --event_stat can not be used at the same time, use -h for detailed help">();
			return -1;
		}

		if (detail_event && (search_event || statistic_event))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ed --event_detail can not be used with -r --regex or -es --event_stat, use -h for detailed help">();
			return -1;
		}

		if (flame_event && (search_event || statistic_event || detail_event))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ef --event_flame_graph can not be used with -r --regex, -es --event_stat or -ed --event_detail, use -h for detailed help">();
			return -1;
		}

		if (search_thread && (search_event || statistic_event || detail_event || flame_event))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ts --thread_search can not be used with -r --regex, -es --event_stat, -ed --event_detail or -ef --event_flame_graph, use -h for detailed help">();
			return -1;
		}

		if (search_thread)
		{
			setting.parser = std::make_unique<ThreadSearchParser>(std::move(thread_config));
		}
		else if (flame_event)
		{
			setting.parser = std::make_unique<EventFlameGraphParser>(std::move(flame_config));
		}
		else if (detail_event)
		{
			setting.parser = std::make_unique<EventDetailParser>(std::move(detail_config));
		}
		else if (statistic_event)
		{
			setting.parser = std::make_unique<EventStatisticParser>(std::move(statistic_config));
		}
		else if (search_event)
		{
			setting.parser = std::make_unique<EventSearchParser>(std::move(search_config));
		}
		else {
			setting.parser = std::make_unique<GameThreadParser>(config);
		}

		return 0;
	}
}
