module;

#include <cassert>
#include <re2/re2.h>

module UEBabyPramInsightFilterEventFilter;

import UEBabyPramInsightFilterParser;
import UEBabyPramInsightFilterGameThread;
import UEBabyPramInsightFilterEventSearch;
import UEBabyPramInsightFilterEventStatistic;
import UEBabyPramInsightFilterThreadSearch;
import UEBabyPramInsightParser;
import Potato;
import std;

namespace
{
	// Query grammar:
	//   query      := chain [ '&' 'Time' '=' timerange+ ]
	//                       [ '&' 'Thread' '=' '(' CaretReg ')' ]
	//   chain      := elem ( '->' elem )*
	//   elem       := '(' CaretReg ')'   // query only
	//               | '[' CaretReg ']'   // query and statistics
	//   timerange  := '[' TimePoint ',' TimePoint ']'
	//   TimePoint  := Integer 'm' Decimal 's' | Decimal 's' | Integer 's'
	//               | Integer 'm' Integer 's'
	//
	// The event chain is required, the time and the thread parts are optional and
	// joined by '&', the 'Time' part may carry more than one time range. The lexical
	// terminals carry the masks CaretReg[1], Integer[2] and Decimal[3], the reduce
	// masks are: 1 Query, 2/3 Chain, 4/5 ChainElem, 6/7/8 QueryTail, 9/10 TimeTail,
	// 11 ThreadCond, 12/13 TimeRanges, 14 TimeRange, 15/16/17/18 TimePoint.
	static std::u8string_view ebnf_string = u8R"(
CaretReg := '\^[^\^]+\^' : [1] ;
Integer  := '[0-9]+' : [2] ;
Decimal  := '[0-9]+\.[0-9]+' : [3] ;
$        := '\s+' ;

%%%%

$ := <Query> ;

<Query> := <Chain> <QueryTail> : [1] ;

<Chain> := <ChainElem> : [2] ;
        := <Chain> '->' <ChainElem> : [3] ;

<ChainElem> := '(' CaretReg ')' : [4] ;
            := '[' CaretReg ']' : [5] ;

<QueryTail> := '&' 'Time' '=' <TimeRanges> <TimeTail> : [6] ;
            := <ThreadCond> : [7] ;
            := : [8] ;

<TimeTail> := <ThreadCond> : [9] ;
           := : [10] ;

<ThreadCond> := '&' 'Thread' '=' '(' CaretReg ')' : [11] ;

<TimeRanges> := <TimeRange> : [12] ;
             := <TimeRanges> <TimeRange> : [13] ;

<TimeRange> := '[' <TimePoint> ',' <TimePoint> ']' : [14] ;

<TimePoint> := Integer 'm' Decimal 's' : [15] ;
           := Decimal 's' : [16] ;
           := Integer 's' : [17] ;
           := Integer 'm' Integer 's' : [18] ;
)";

	using UEBabyPram::InsightFilter::EventQuery;
	using UEBabyPram::InsightParser::DurationSec;
	using Potato::Misc::IndexSpan;

	// The partial query which the QueryTail and the TimeTail build.
	struct QueryTailData
	{
		std::vector<IndexSpan<DurationSec>> time_ranges;
		std::shared_ptr<re2::RE2> thread_matcher;
	};

	using TimeRange = IndexSpan<DurationSec>;

	// Compile the regular expression which is carried by the CaretReg token, the token text
	// is "^pattern^" and the delimiters are stripped. Throws std::u8string, which is the message
	// which ParseEventQuery returns, when re2 does not accept the pattern.
	std::shared_ptr<re2::RE2> BuildMatcher(std::u8string_view token)
	{
		if (token.size() < 2)
		{
			std::u8string message = u8"the regular expression is not delimited by '^': ";
			message += token;
			throw message;
		}

		std::string_view pattern{ reinterpret_cast<char const*>(token.data()) + 1, token.size() - 2 };
		re2::RE2::Options options;
		// The error is returned by ParseEventQuery, re2 should not print it a second time.
		options.set_log_errors(false);
		auto matcher = std::make_shared<re2::RE2>(pattern, options);
		if (!matcher->ok())
		{
			std::u8string message = u8"the regular expression is not acceptable: ";
			// The message of re2 is ASCII, so the encoding never fails, and it usually names the
			// pattern itself, ex: "missing ): foo(".
			Potato::Encode::UnicodeEncoder<char, char8_t>::EncodeTo(
				std::string_view{ matcher->error() },
				std::back_inserter(message)
			);
			throw message;
		}
		return matcher;
	}

	std::size_t BuildInteger(std::u8string_view token)
	{
		std::size_t value = 0;
		char const* begin = reinterpret_cast<char const*>(token.data());
		char const* end = begin + token.size();
		auto result = std::from_chars(begin, end, value);
		if (result.ec != std::errc{} || result.ptr != end)
		{
			std::u8string message = u8"the integer is not acceptable: ";
			message += token;
			throw message;
		}
		return value;
	}

	double BuildDecimal(std::u8string_view token)
	{
		double value = 0.0;
		char const* begin = reinterpret_cast<char const*>(token.data());
		char const* end = begin + token.size();
		auto result = std::from_chars(begin, end, value, std::chars_format::general);
		if (result.ec != std::errc{} || result.ptr != end)
		{
			std::u8string message = u8"the decimal is not acceptable: ";
			message += token;
			throw message;
		}
		return value;
	}

	DurationSec BuildDuration(std::size_t minutes, double seconds)
	{
		return DurationSec{ static_cast<double>(minutes) * 60.0 + seconds };
	}

	std::vector<TimeRange> BuildTimeRanges(TimeRange range)
	{
		std::vector<TimeRange> ranges;
		ranges.push_back(range);
		return ranges;
	}
}


namespace UEBabyPram::InsightFilter
{
	Potato::EBNF::Ebnf const& GetFilterEbnf()
	{
		static Potato::EBNF::Ebnf const ebnf{ ebnf_string };
		return ebnf;
	}

	std::variant<EventQuery, std::u8string> ParseEventQuery(std::u8string_view query)
	{
		using namespace Potato::EBNF;

		try
		{
			EbnfProcessor processor;

			auto handle_symbol = [&](SymbolInfo symbol, std::size_t user_mask) -> std::any
			{
				std::u8string_view token = symbol.TokenIndex.Slice(query);
				switch (user_mask)
				{
				case 1:
					return BuildMatcher(token);
				case 2:
					return BuildInteger(token);
				case 3:
					return BuildDecimal(token);
				default:
					return {};
				}
			};

			auto handle_reduce = [](SymbolInfo, ReduceProduction production) -> std::any
			{
				switch (production.UserMask)
				{
				case 1:
				{
					EventQuery result;
					result.chain = production[0].Consume<std::vector<EventQuery::ChainElement>>();
					QueryTailData tail = production[1].Consume<QueryTailData>();
					result.time_ranges = std::move(tail.time_ranges);
					result.thread_matcher = std::move(tail.thread_matcher);
					return result;
				}
				case 2:
				{
					std::vector<EventQuery::ChainElement> chain;
					chain.push_back(production[0].Consume<EventQuery::ChainElement>());
					return chain;
				}
				case 3:
				{
					auto chain = production[0].Consume<std::vector<EventQuery::ChainElement>>();
					chain.push_back(production[2].Consume<EventQuery::ChainElement>());
					return chain;
				}
				case 4:
				{
					return EventQuery::ChainElement{
						EventQuery::ChainElement::Kind::QueryOnly,
						production[1].Consume<std::shared_ptr<re2::RE2>>()
					};
				}
				case 5:
				{
					return EventQuery::ChainElement{
						EventQuery::ChainElement::Kind::QueryAndStatistic,
						production[1].Consume<std::shared_ptr<re2::RE2>>()
					};
				}
				case 6:
				{
					QueryTailData tail;
					tail.time_ranges = production[3].Consume<std::vector<TimeRange>>();
					tail.thread_matcher = production[4].Consume<std::shared_ptr<re2::RE2>>();
					return tail;
				}
				case 7:
				{
					QueryTailData tail;
					tail.thread_matcher = production[0].Consume<std::shared_ptr<re2::RE2>>();
					return tail;
				}
				case 8:
					return QueryTailData{};
				case 9:
					return production[0].Consume<std::shared_ptr<re2::RE2>>();
				case 10:
					return std::shared_ptr<re2::RE2>{};
				case 11:
					return production[4].Consume<std::shared_ptr<re2::RE2>>();
				case 12:
					return BuildTimeRanges(production[0].Consume<TimeRange>());
				case 13:
				{
					auto ranges = production[0].Consume<std::vector<TimeRange>>();
					ranges.push_back(production[1].Consume<TimeRange>());
					return ranges;
				}
				case 14:
				{
					DurationSec begin = production[1].Consume<DurationSec>();
					DurationSec end = production[3].Consume<DurationSec>();
					return TimeRange{ begin, end };
				}
				case 15:
					return BuildDuration(production[0].Consume<std::size_t>(), production[2].Consume<double>());
				case 16:
					return DurationSec{ production[0].Consume<double>() };
				case 17:
					return DurationSec{ static_cast<double>(production[0].Consume<std::size_t>()) };
				case 18:
					return BuildDuration(production[0].Consume<std::size_t>(), static_cast<double>(production[2].Consume<std::size_t>()));
				default:
					return {};
				}
			};

			processor.SetObserverTable(GetFilterEbnf(), handle_symbol, handle_reduce);

			if (!Process(processor, query))
			{
				std::u8string message = u8"the query does not follow the grammar which GetFilterEbnf() describes";
				// The processor stops at the text which it is not able to accept and reports the
				// position where it stopped, so the rest of the text is the offending part. The
				// position is clamped, it points to the end of the text when the text ended too
				// early.
				std::size_t position = std::min(processor.GetRequireTokenIndex(), query.size());
				if (position < query.size())
				{
					message += u8", the text from here is not accepted: <";
					message += query.substr(position);
					message += u8">";
				}
				else if (query.empty())
				{
					message += u8", the query is empty";
				}
				else {
					message += u8", the text ended too early";
				}
				return message;
			}

			return processor.GetData<EventQuery>();
		}
		catch (std::u8string error)
		{
			return error;
		}
		catch (std::exception const& error)
		{
			// The query is built with std::any, so a failed type cast (std::bad_any_cast) is the
			// only expected standard exception here, it means that the text is not acceptable.
			std::u8string message = u8"the query is not acceptable: ";
			Potato::Encode::UnicodeEncoder<char, char8_t>::EncodeTo(
				std::string_view{ error.what() },
				std::back_inserter(message)
			);
			return message;
		}
	}
}
