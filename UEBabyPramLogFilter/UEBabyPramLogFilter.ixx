module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;
import UEBabyPramLogParser;


export namespace UEBabyPram::LogFilter
{
	enum class OutputTarget
	{
		FILE,
		STD,
	};

	enum class OutputMode
	{
		FILTER,
		FILTER_WITH_FRAME_SPERATE,
		CUSTOM,
	};

	constexpr std::size_t max_cache_size_gb = 1;

	struct FilterSetting
	{
		std::vector<std::filesystem::path> input_files;
		std::vector<std::filesystem::path> input_paths;
		std::filesystem::path output_paths;
		std::filesystem::path output_expand;
		OutputTarget target = OutputTarget::FILE;
		OutputMode mode = OutputMode::FILTER;
		std::size_t max_cache_size = 1024 * 1204 * 1024 * max_cache_size_gb;
	};

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
		std::variant<std::monostate, std::size_t, LogParser::LogLine::TimeT, Potato::Reg::Dfa, std::u8string> value;

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
		bool AddStatement(std::u8string_view statement, std::pmr::u8string& error_message);
		bool AddStatement(std::u8string_view statement)
		{
			std::pmr::u8string error_message;
			return AddStatement(statement, error_message);
		}
		std::optional<bool> Detect(LogParser::LogLine const& log) { 
			if (statement)
			{
				return statement->Detect(log, dfa_processor);
			}
			return std::nullopt;
		}
		static std::shared_ptr<StatementInterface> ComplierStatement(std::u8string_view statement, std::pmr::u8string& error_message);
	protected:
		std::shared_ptr<StatementInterface> statement;
		Potato::Reg::DfaProcessor dfa_processor;
	};

	struct UnsupportReg
	{
		std::pmr::u8string error_message;
	};

	struct UnsupportTime
	{
		std::pmr::u8string error_message;
	};
}