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
import UEBabyPramInsightFilterMemoryStatistic;
import UEBabyPramInsightFilterMemoryTagSearch;
import UEBabyPramInsightFilterMemoryValue;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace UEBabyPram::InsightFilter
{
	using namespace Potato;

	namespace
	{
		// The version of the tool, which -v --version reports. It is kept in the command line of this tool only, so the
		// log filter carries its own version and a new version of one tool never rebuilds the other one.
		constexpr std::string_view version = "1.2";

		void PrintGeneralHelp()
		{
			// The help text is longer than the limit of the compile time string template argument (8192 characters), so it
			// is printed in parts: the first part carries the header and the parts which follow only continue the text, the
			// newline which a part ends with is the one which the log appends by itself.
			Log::Log<CommandLineLogCategory, Log::LogLevel::Display, LR"HELP(
UEBabyPramInsightFilter, report the GameThread timing and the memory channel of an unreal insight .utrace file.

Usage: UEBabyPramInsightFilter -f <trace> [mode] [options]
  The default mode reports the GameThread frames, the other modes report what an option asks for and
  only one of them runs at a time. Every report goes to the log unless -o or -a names a file.

  Query, which the modes -es, -ed, -ef, -ms and -mv carry:
    <query>   := <chain> [ '&' ( Time | ITime ) '=' <slices> ] [ '&' Thread=(^regex^) ]
                     [ '&' Tracker=(^regex^) ]
    <chain>   := <element> ( '->' <element> )*
    <element> := % | (^regex^) | [^regex^] | %[^regex^]
    <slices>  := '[' <time> ',' <time> ']' ...     <time> := 90s | 1m30s | 1m30.5s | 30.5s

    Every regular expression matches the whole name, and the default mode takes no regular
    expression: its GameThread is the thread whose name is exactly GameThread.
    The chain is the call chain of the events, and
    the chain of a memory tag is the segments of its name, so (^A^)->(^B^) selects A/B exactly; %
    matches any event or any segment, which is the way to walk one level deeper.
    Time selects an occurrence when its time range overlaps a slice, ITime selects it only when a
    slice contains it or is equal to it. The parsers of the memory channel read the time point of a
    snapshot instead of a range, so Time and ITime select the same snapshots there.
    Thread selects the thread of the call stacks, Tracker selects the memory tracker.
    Every mode reads only the conditions which it needs and rejects the others, so a query which
    was written for another mode is reported instead of selecting more data than expected:
      -es, -ed, -ef  read the chain, Time/ITime and Thread
      -ms, -mv       read the chain, Time/ITime and Tracker
)HELP">();

			Log::LogTo<Log::DefaultLogFormatter, LR"HELP(
  -r, --regex <regex>
      Report the location of every event whose whole name is matched, -mlc limits the count.

  -es, --event_stat <query>
      The statistics of every [^regex^] element of the query: the count, the total, the mean and the
      median duration, the top occurrences (-tx) and their duration histogram (-dbc). The query may
      be given more than once, and every query keeps its own statistics.

  -ed, --event_detail <query>
      The stack trees which complete the query, with the whole call tree of every one of them
      (-mvc, -mnc and -md).

  -ef, --event_flame_graph <query>
      The same trees folded into a flame graph, or into a folded stack file when -fs names one.

  -ts, --thread_search <regex>
      Every thread whose whole name is matched.

  -ms, --memory_stat <query>
      The statistics of every memory tag which the query selects, per tracker: the first, the last,
      the largest and the smallest value, the mean, and the trend of the tag: the interval between
      the first largest value and the first smallest one, in the order of the time, the value at 20%,
      40%, 60% and 80% of it, the slope of every one of those parts and the standard deviation of the
      slopes. -msr orders the rows and -mst limits how many of them are printed.

  -mv, --memory_value <query>
      The shape of the time line of every selected tag: the first sample, the peaks, which are the
      samples where the slope of the values turns, and the last sample, every one of them with its
      time, its value and the slope of the values at it. A peak which is less than 5% away from the
      peak before it is a small wave and is dropped. -mvp limits the peaks of one tag, -mvs keeps two
      printed peaks that many samples apart, and --all_samples prints every sample instead.

  -mt, --memory_tag <regex>
      Every memory spec whose whole name is matched: the tags, the memory trackers and the tag sets.

  -mtt, --memory_tracker <regex>
      Only the memory trackers whose whole name is matched, which is the spec of the LLM channel
      which produces the snapshots.

  --all_samples
      Print every sample of every tag which -mv selects instead of its peaks, which is the mode of
      the whole time line; -mvp and -mvs do not apply to it.
)HELP">(*Log::GetSTDOutPrinter());

			Log::LogTo<Log::DefaultLogFormatter, LR"HELP(
  -f, --file <path>            The .utrace file to analyze.
  -o, --output <path>          Write the report into the file instead of the log.
  -a, --append <path>          Append the report to the file instead of the log, -a and -o can not
                               be used at the same time.
  -csv, --csv <path>           Write the report as CSV instead of the text report, for -es, -ms, -mv
                               and the default mode.
  -fs, --folded_stack <path>   Write the flame graph of -ef as a folded stack file: one call path
                               per line with the self time of the path in microseconds.
  -mlc, --max_location_count <count>
                               How many event locations -r reports, 0 for no limit, default is 256.
  -tx, --top_count <count>     How many occurrences -es reports in its top list, default is 5.
  -dbc, --duration_bucket_count <count>
                               How many buckets the duration histogram of -es carries, default is 5.
  -mvc, --max_view_count <count>
                               How many of the longest stack trees -ed and -ef keep, default is 10.
                               Unlike -mnc and -md, 0 keeps no view at all, so the report is empty.
  -mnc, --max_node_count <count>
                               How many nodes of one reported tree -ed prints, 0 for no limit, the
                               nodes over it and the whole subtree behind them are left out and marked
                               with a single '...', default is 100.
  -md, --max_depth <depth>     How deep one reported tree -ed is printed, 0 for no limit, the nodes
                               below the depth are marked with one '...' per level, default is 8.
  -msr, --memory_sort <name|mean|max>
                               The order of the rows of -ms: name is the order of the tracker and then
                               of the tag, mean and max put the largest value first, default is name.
  -mst, --max_tag_count <count>
                               How many statistics -ms prints, 0 for no limit, default is 0.
  -mvp, --max_peak_count <count>
                               How many peaks of one tag -mv prints, 0 for no limit, default is 20.
  -mvs, --min_peak_sample_count <count>
                               How many samples are between two printed peaks at least, default is 5.
  -mrf, --max_record_frame <count>
                               How many of the longest GameThread frames the default mode records,
                               default is 10.
  -metc, --max_exclude_time_count <count>
                               How many exclude time events its top lists keep, default is 30.
  -sr, --sampling_ratio <ratio>
                               The ratio of the time range which the default mode analyzes, between
                               0.0 and 1.0, default is 0.9: the beginning and the end of a trace are
                               the time when the trace is saved, so their frames are not accurate. The
                               report covers the frames whose time range intersects the sampled range,
                               and it reports the sampled range and the ratio which was used.
  -h, --help                   Print this help.
  -v, --version                Print the version of the tool.

  Restrictions, which the command line reports as an error instead of ignoring:
    -csv prints the report itself, so it can only be given with -es, -ms, -mv or the default mode,
    and it can not be used with -o or -a.
    -fs prints the flame graph itself, so it can only be given with -ef, and it can not be used
    with -o or -a either.
    -mt and -mtt can not be used at the same time.
    The modes are exclusive as well: only one of -r, -es, -ed, -ef, -ts, -ms, -mv, -mt and -mtt
    may be given.
)HELP">(*Log::GetSTDOutPrinter());
		}
	}

	// Whether the query carries a condition which the parser does not read. It is reported and ends the command: a
	// condition which a parser does not read is rejected instead of being ignored, so a query which was written for
	// another mode is reported instead of selecting more data than the caller expects. The mode and the option are only
	// used for the message.
	bool ReportRejectedQueryCondition(
		EventQuery const& query,
		QueryConditionFlag accepted,
		std::wstring_view mode,
		std::wstring_view option
	)
	{
		std::u8string error = CheckQueryCondition(query, accepted);
		if (error.empty())
		{
			return true;
		}

		std::wstring message;
		Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(error, std::back_inserter(message));
		Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"{} {} <query> : <{}>, use -h for detailed help">(
			mode,
			option,
			message
		);
		return false;
	}

	int HandleCommand(int argc, char* argv[], CommandLineSetting& setting)
	{
		GameThreadParser::Config config;
		EventSearchParser::Config search_config;
		EventStatisticParser::Config statistic_config;
		EventDetailParser::Config detail_config;
		EventFlameGraphParser::Config flame_config;
		ThreadSearchParser::Config thread_config;
		MemoryStatisticParser::Config memory_config;
		MemoryValueParser::Config memory_value_config;
		MemoryTagSearchParser::Config memory_tag_search_config;
		bool search_event = false;
		bool statistic_event = false;
		// The CSV file is the format of two reports: the statistics of -es and the frames of the default mode. It names
		// the file which the report is written into itself, so it is not mixed with -o, and every other mode reports a
		// text which has no CSV form yet.
		std::optional<std::filesystem::path> csv_path;
		// Whether the flame graph of -ef is written into a folded stack file, the path is taken from -fs.
		bool folded_stack_event = false;
		bool detail_event = false;
		bool flame_event = false;
		bool search_thread = false;
		bool statistic_memory = false;
		bool statistic_memory_value = false;
		bool search_memory_tag = false;
		// Whether the report file is named by -o or by -a, the two can not be given at the same time.
		bool output_given = false;
		bool append_given = false;
		std::u8string path_buffer;

		for (std::size_t i = 1; i < argc; ++i)
		{
			std::string_view argv_string = argv[i];

			if (argv_string == "-h" || argv_string == "--help")
			{
				PrintGeneralHelp();
				return 1;
			}
			else if (argv_string == "-v" || argv_string == "--version")
			{
				// The version is reported with the name of the tool, so the output of the tools of the repository is told
				// apart by a script which reads it, and it is the version which this tool carries itself. It is written
				// without the prefix of the log, so the line is exactly what a script reads.
				OutputLog<L"UEBabyPramInsightFilter <{}>">(*Log::GetSTDOutPrinter(), version);
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
					output_given = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -o --output, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-a" || argv_string == "--append")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });
					setting.output_path = std::filesystem::path{ path_buffer };
					setting.append = true;
					append_given = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -a --append, use -h for detailed help">();
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

					// The parser reads the time and the thread conditions, a query which carries another condition is
					// rejected here, before the trace is read.
					auto& query = std::get<EventQuery>(query_result);
					if (!ReportRejectedQueryCondition(query, EventStatisticParser::AcceptedQueryCondition(), L"-es", L"--event_stat"))
					{
						return -1;
					}

					// The queries are accumulated, every one of them keeps its own statistics.
					statistic_config.queries.push_back(std::move(query));
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

					auto& query = std::get<EventQuery>(query_result);
					if (!ReportRejectedQueryCondition(query, EventDetailParser::AcceptedQueryCondition(), L"-ed", L"--event_detail"))
					{
						return -1;
					}

					// The detail parser keeps the stack trees which the query matches, only one query
					// is accepted by it, and the text is reported with the saved views.
					detail_config.query = std::move(query);
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

					auto& query = std::get<EventQuery>(query_result);
					if (!ReportRejectedQueryCondition(query, EventFlameGraphParser::AcceptedQueryCondition(), L"-ef", L"--event_flame_graph"))
					{
						return -1;
					}

					// The flame graph parser aggregates the stack trees which the query matches, only
					// one query is accepted by it, and the text is reported with the graph.
					flame_config.query = std::move(query);
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
			else if (argv_string == "-ms" || argv_string == "--memory_stat")
			{
				if (i + 1 < argc)
				{
					// The query is built from the argument, and the error of an unacceptable query is reported here, so the
					// parser only receives the queries which are ready to be analyzed.
					std::u8string query_text;
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ query_text });
					auto query_result = ParseEventQuery(query_text);
					if (auto error = std::get_if<std::u8string>(&query_result); error != nullptr)
					{
						std::wstring message;
						Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(*error, std::back_inserter(message));
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ms --memory_stat <query> : <{}>, use -h for detailed help">(
							message
						);
						return -1;
					}

					// The memory parsers read the time and the tracker conditions, the thread condition belongs to the call
					// stacks, so a query which carries it is rejected here instead of counting every tracker.
					auto& query = std::get<EventQuery>(query_result);
					if (!ReportRejectedQueryCondition(query, MemoryStatisticParser::AcceptedQueryCondition(), L"-ms", L"--memory_stat"))
					{
						return -1;
					}

					// The queries are accumulated, every one of them keeps its own statistics.
					memory_config.queries.push_back(std::move(query));
					statistic_memory = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -ms --memory_stat, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mv" || argv_string == "--memory_value")
			{
				if (i + 1 < argc)
				{
					// The query is read the same way as the query of -ms, and the error of an unacceptable one is reported
					// here as well.
					std::u8string query_text;
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ query_text });
					auto query_result = ParseEventQuery(query_text);
					if (auto error = std::get_if<std::u8string>(&query_result); error != nullptr)
					{
						std::wstring message;
						Encode::UnicodeEncoder<char8_t, wchar_t>::EncodeTo(*error, std::back_inserter(message));
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mv --memory_value <query> : <{}>, use -h for detailed help">(
							message
						);
						return -1;
					}

					auto& query = std::get<EventQuery>(query_result);
					if (!ReportRejectedQueryCondition(query, MemoryValueParser::AcceptedQueryCondition(), L"-mv", L"--memory_value"))
					{
						return -1;
					}

					// The queries are accumulated, every one of them keeps its own time lines.
					memory_value_config.queries.push_back(std::move(query));
					statistic_memory_value = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mv --memory_value, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "--all_samples")
			{
				// The mode which prints the whole time line of every selected tag instead of its peaks.
				memory_value_config.all_samples = true;
			}
			else if (argv_string == "-mvp" || argv_string == "--max_peak_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						memory_value_config.max_peak_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mvp --max_peak_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mvp --max_peak_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mvs" || argv_string == "--min_peak_sample_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						memory_value_config.min_peak_sample_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mvs --min_peak_sample_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mvs --min_peak_sample_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-msr" || argv_string == "--memory_sort")
			{
				if (i + 1 < argc)
				{
					std::string_view order = argv[i + 1];
					if (order == "name")
					{
						memory_config.sort = MemoryStatisticParser::SortKind::Name;
						++i;
					}
					else if (order == "mean")
					{
						memory_config.sort = MemoryStatisticParser::SortKind::Mean;
						++i;
					}
					else if (order == "max")
					{
						memory_config.sort = MemoryStatisticParser::SortKind::Max;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-msr --memory_sort <name|mean|max> : <{}> is not an acceptable order, use -h for detailed help">(order);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -msr --memory_sort, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mst" || argv_string == "--max_tag_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						memory_config.max_tag_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mst --max_tag_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mst --max_tag_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mt" || argv_string == "--memory_tag")
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
						if (memory_tag_search_config.kind == MemoryTagSearchParser::Kind::Tracker)
						{
							Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mt --memory_tag and -mtt --memory_tracker can not be used at the same time, use -h for detailed help">();
							return -1;
						}

						memory_tag_search_config.matcher = std::move(matcher);
						memory_tag_search_config.kind = MemoryTagSearchParser::Kind::All;
						search_memory_tag = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mt --memory_tag <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mt --memory_tag, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-mtt" || argv_string == "--memory_tracker")
			{
				if (i + 1 < argc)
				{
					if (search_memory_tag)
					{
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mt --memory_tag and -mtt --memory_tracker can not be used at the same time, use -h for detailed help">();
						return -1;
					}

					std::string_view pattern = argv[i + 1];
					re2::RE2::Options options;
					// The error is reported below, re2 should not print it a second time.
					options.set_log_errors(false);
					auto matcher = std::make_shared<re2::RE2>(pattern, options);
					if (matcher->ok())
					{
						// Only the memory trackers are searched, which is the spec of the LLM channel which produces the
						// snapshots, so the tags and the tag sets are left out of the report.
						memory_tag_search_config.matcher = std::move(matcher);
						memory_tag_search_config.kind = MemoryTagSearchParser::Kind::Tracker;
						search_memory_tag = true;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mtt --memory_tracker <regex> : <{}> is not an acceptable regex : <{}>, use -h for detailed help">(
							pattern,
							std::string_view{ matcher->error() }
						);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mtt --memory_tracker, use -h for detailed help">();
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
			else if (argv_string == "-dbc" || argv_string == "--duration_bucket_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						statistic_config.duration_bucket_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-dbc --duration_bucket_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -dbc --duration_bucket_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-csv" || argv_string == "--csv")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });

					// The file is opened by the parser which owns the report when the report is written, so the path is
					// only stored here, and the parser which the path belongs to is decided after every option is read.
					// The error of a path which can not be opened is reported by that parser then.
					csv_path = std::filesystem::path{ path_buffer };
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -csv --csv, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-fs" || argv_string == "--folded_stack")
			{
				if (i + 1 < argc)
				{
					path_buffer.clear();
					Encode::STDInputEncoder<char8_t>::EncodeTo(argv[i + 1], std::back_insert_iterator{ path_buffer });

					// The file is opened by the flame graph parser when the graph is written, so the path is only stored
					// here, the error of a path which can not be opened is reported then.
					flame_config.folded_stack_path = std::filesystem::path{ path_buffer };
					folded_stack_event = true;
					++i;
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -fs --folded_stack, use -h for detailed help">();
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
			else if (argv_string == "-mnc" || argv_string == "--max_node_count")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						detail_config.max_node_count = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mnc --max_node_count <count> : <{}> is not a valid count, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -mnc --max_node_count, use -h for detailed help">();
					return -1;
				}
			}
			else if (argv_string == "-md" || argv_string == "--max_depth")
			{
				if (i + 1 < argc)
				{
					std::size_t value = 0;
					std::string_view sub_argv = argv[i + 1];
					if (Format::DirectDeformat(sub_argv, value))
					{
						detail_config.max_depth = value;
						++i;
					}
					else {
						Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-md --max_depth <depth> : <{}> is not a valid depth, use -h for detailed help">(sub_argv);
						return -1;
					}
				}
				else {
					Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"Unsupport command -md --max_depth, use -h for detailed help">();
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

		// -o replaces the file and -a keeps its previous content, so the report file is named by exactly one of them.
		if (output_given && append_given)
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-o --output and -a --append can not be used at the same time, use -h for detailed help">();
			return -1;
		}

		// The CSV file is the format of four reports: the statistics of -es, the statistics and the time lines of the memory
		// tags of -ms and -mv, and the frames of the default mode, which is the mode of no other option. The file names
		// itself, so it is not mixed with -o or -a.
		if (csv_path)
		{
			if (statistic_event)
			{
				statistic_config.csv_path = std::move(*csv_path);
				csv_path.reset();
			}
			else if (statistic_memory)
			{
				memory_config.csv_path = std::move(*csv_path);
				csv_path.reset();
			}
			else if (statistic_memory_value)
			{
				memory_value_config.csv_path = std::move(*csv_path);
				csv_path.reset();
			}
			else if (!search_event && !detail_event && !flame_event && !search_thread && !search_memory_tag && !statistic_memory && !statistic_memory_value)
			{
				config.csv_path = std::move(*csv_path);
				csv_path.reset();
			}
			else
			{
				Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-csv --csv can only be used with -es --event_stat, -ms --memory_stat, -mv --memory_value or the default mode, use -h for detailed help">();
				return -1;
			}

			if (!setting.output_path.empty())
			{
				Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-csv --csv and -o --output or -a --append can not be used at the same time, use -h for detailed help">();
				return -1;
			}
		}

		// The folded stack file is the format of the flame graph, so it is only accepted with -ef, and it names the file
		// which the graph is written into itself, so it is not mixed with -o.
		if (folded_stack_event && !flame_event)
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-fs --folded_stack can only be used with -ef --event_flame_graph, use -h for detailed help">();
			return -1;
		}

		if (folded_stack_event && !setting.output_path.empty())
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-fs --folded_stack and -o --output or -a --append can not be used at the same time, use -h for detailed help">();
			return -1;
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

		if (flame_event && (search_event || statistic_event || detail_event || statistic_memory || statistic_memory_value || search_memory_tag))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ef --event_flame_graph can not be used with -r --regex, -es --event_stat, -ed --event_detail, -ms --memory_stat, -mv --memory_value or -mt --memory_tag, use -h for detailed help">();
			return -1;
		}

		if (search_thread && (search_event || statistic_event || detail_event || flame_event || statistic_memory || statistic_memory_value || search_memory_tag))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ts --thread_search can not be used with -r --regex, -es --event_stat, -ed --event_detail, -ef --event_flame_graph, -ms --memory_stat, -mv --memory_value or -mt --memory_tag, use -h for detailed help">();
			return -1;
		}

		if (statistic_memory && (search_event || statistic_event || detail_event || flame_event || search_thread || statistic_memory_value || search_memory_tag))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-ms --memory_stat can not be used with -r --regex, -es --event_stat, -ed --event_detail, -ef --event_flame_graph, -ts --thread_search, -mv --memory_value or -mt --memory_tag, use -h for detailed help">();
			return -1;
		}

		if (statistic_memory_value && (search_event || statistic_event || detail_event || flame_event || search_thread || statistic_memory || search_memory_tag))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mv --memory_value can not be used with -r --regex, -es --event_stat, -ed --event_detail, -ef --event_flame_graph, -ts --thread_search, -ms --memory_stat or -mt --memory_tag, use -h for detailed help">();
			return -1;
		}

		if (search_memory_tag && (search_event || statistic_event || detail_event || flame_event || search_thread || statistic_memory || statistic_memory_value))
		{
			Log::Log<CommandLineLogCategory, Log::LogLevel::Error, L"-mt --memory_tag can not be used with -r --regex, -es --event_stat, -ed --event_detail, -ef --event_flame_graph, -ts --thread_search, -ms --memory_stat or -mv --memory_value, use -h for detailed help">();
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
		else if (search_memory_tag)
		{
			setting.parser = std::make_unique<MemoryTagSearchParser>(std::move(memory_tag_search_config));
		}
		else if (statistic_memory_value)
		{
			setting.parser = std::make_unique<MemoryValueParser>(std::move(memory_value_config));
		}
		else if (statistic_memory)
		{
			setting.parser = std::make_unique<MemoryStatisticParser>(std::move(memory_config));
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
