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
		virtual std::optional<bool> Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const = 0;
	};

	struct ConditionStatement : StatementInterface
	{
		PropertyType property;
		CompareType compare;
		std::variant<std::monostate, std::size_t, LogParser::LogLine::TimeT, std::shared_ptr<re2::RE2>, std::u8string> value;

		virtual std::optional<bool> Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const override;
	};

	struct OperatorStatement : StatementInterface
	{
		bool is_or = false;
		std::shared_ptr<StatementInterface> statement_1;
		std::shared_ptr<StatementInterface> statement_2;
		virtual std::optional<bool> Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const override;
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
		std::optional<bool> Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& dfa_processor) {
			if (statement)
			{
				return statement->Detect(log, dfa_processor);
			}
			return std::nullopt;
		}
		static std::shared_ptr<StatementInterface> ComplierStatement(std::u8string_view statement, std::pmr::u8string& error_message);
		operator bool() const { return statement.operator bool(); }
	protected:
		std::shared_ptr<StatementInterface> statement;
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