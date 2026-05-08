module;

#include <cassert>

export module UEBabyPramLogFilter;

import std;
import Potato;
import UEBabyPram;


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

	void Test();

	enum class PropertyType
	{
		Time,
		Level,
		Line,
		Log,
		Category,
		StatementAnd,
		StatementOr
	};

	enum class CompareType
	{
		Smaller,
		SmallerEqual,
		Equal,
		BiggerEqual,
		Bigger,
	};

	struct ConditionStatement
	{
		PropertyType property;
		CompareType compare;
		std::variant<std::monostate, std::size_t, LogParser::LogLine::TimeT, Potato::Reg::Dfa, std::u8string> value;

		std::optional<bool> Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const;
		static std::optional<bool> Detect(std::span<ConditionStatement const> statemenets, LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor);
	};

	struct LogFilterProcessor
	{
		LogFilterProcessor();
		void Init(std::u8string_view statement);
		std::optional<bool> Detect(LogParser::LogLine const& log) { 
			return ConditionStatement::Detect(std::span(statement.data(), statement.size()), log, dfa_processor);
		}
	protected:
		std::pmr::vector<ConditionStatement> statement;
		Potato::Reg::DfaProcessor dfa_processor;
	};


}