module;

#include <cassert>
#include <re2/re2.h>

export module UEBabyPramLogFilter;

import std;
import Potato;
import UEBabyPramLogParser;


export namespace UEBabyPram::LogFilter
{

	enum class PropertyType
	{
		Time,
		Level,
		Line,
		Log,
		Category,
	};

	enum class CompareType
	{
		Smaller,
		SmallerEqual,
		Equal,
		BiggerEqual,
		Bigger,
	};

	struct StatementInterface
	{
		virtual std::optional<bool> Detect(LogParser::LogLine const& log) const = 0;
	};

	struct ConditionStatement : StatementInterface
	{
		PropertyType property;
		CompareType compare;
		std::variant<std::monostate, std::size_t, LogParser::LogLine::TimeT, std::shared_ptr<re2::RE2>, std::u8string> value;

		virtual std::optional<bool> Detect(LogParser::LogLine const& log) const override;
	};

	struct OperatorStatement : StatementInterface
	{
		bool is_or = false;
		std::shared_ptr<StatementInterface> statement_1;
		std::shared_ptr<StatementInterface> statement_2;
		virtual std::optional<bool> Detect(LogParser::LogLine const& log) const override;
	};

	struct LogFilterProcessor
	{
		LogFilterProcessor() {}
		LogFilterProcessor(LogFilterProcessor const&) = default;
		bool AddStatement(std::u8string_view statement, std::pmr::u8string& error_message);
		bool AddStatement(std::u8string_view statement)
		{
			std::pmr::u8string error_message;
			return AddStatement(statement, error_message);
		}
		std::optional<bool> Detect(LogParser::LogLine const& log) {
			if (statement)
			{
				return statement->Detect(log);
			}
			return std::nullopt;
		}
		static std::shared_ptr<StatementInterface> ComplierStatement(std::u8string_view statement, std::pmr::u8string& error_message);
		operator bool() const { return statement.operator bool(); }
	protected:
		std::shared_ptr<StatementInterface> statement;
	};

	struct Filter
	{
		bool Format(UEBabyPram::LogParser::LogLine const& line, std::pmr::string& output_target);
		std::shared_ptr<re2::RE2> matched_regex;
		std::pmr::vector<std::variant<std::u8string, PropertyType, std::size_t>> value;
	};

	struct LogFilterFormatter
	{
		LogFilterFormatter() {}
		
		bool AddStatement(std::string_view regstatement, std::string_view filter_type, std::u8string& error_message);

		bool Format(UEBabyPram::LogParser::LogLine const& line, std::pmr::string& output_target);
		std::pmr::vector<Filter> filters;
	};

	struct UnsupportReg
	{
		std::pmr::u8string error_message;
	};

	struct UnsupportTime
	{
		std::pmr::u8string error_message;
	};

	std::u8string_view GetEbnfString();
}