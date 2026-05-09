module;

module UEBabyPramLogFilter;
import Potato;

namespace UEBabyPram::LogFilter
{
	static constexpr std::u8string_view ebnf_string = u8R"(
		$:='\s+';
		INT:='[1-9][0-9]*' : [1];
		STR:='\^.*?[^\\]\^' : [2];
		STR:='R\^.*?[^\\]\^' : [3];
		COMPARE:='<' : [4];
		COMPARE:='<=' : [5];
		COMPARE:='==' : [6];
		COMPARE:='>' : [7];
		COMPARE:='>=':[8];
		LOGLEVEL:='Fatal':[10];
		LOGLEVEL:='Error':[11];
		LOGLEVEL:='Warning':[12];
		LOGLEVEL:='Display':[13];
		LOGLEVEL:='Log':[14];
		LOGLEVEL:='Verbose':[15];
		LOGLEVEL:='VeryVerbose':[16];
		%%%%
		$:=<Exp>;
		<TIME>:=INT '.' INT '.' INT ':' INT : [2];
			:=INT '.' INT '.' INT : [2];
		<STAT>:='$Time' COMPARE <TIME> : [2];
			:='$Level' COMPARE LOGLEVEL : [2];
			:='$Line' COMPARE INT : [2];
			:='$Log' COMPARE STR : [2];
			:='$Category' COMPARE STR : [2];
			:= '(' <STAT> ')' : [4];
			:= <STAT> '&&' <STAT>  : [4];
			:= <STAT> '||' <STAT>  : [4];
		<Exp>:=<STAT> ';' :[4];
			:=;
		%%%%
		+('&&');
		+('||');
	)";

	Potato::EBNF::Ebnf const& GetEbnf()
	{
		static Potato::EBNF::Ebnf ebnf(ebnf_string);
		return ebnf;
	}

	std::optional<bool> ConditionStatement::Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const
	{
		switch (property)
		{
		case PropertyType::Line:
			if (std::holds_alternative<std::size_t>(value))
			{
				std::size_t target_line = std::get<std::size_t>(value);
				switch(compare)
				{
				case CompareType::Bigger:
					return log.line.Begin() > target_line;
				case CompareType::BiggerEqual:
					return log.line.Begin() >= target_line;
				case CompareType::Equal:
					return log.line.Begin() == target_line;
				case CompareType::Smaller:
					return log.line.Begin() < target_line;
				case CompareType::SmallerEqual:
					return log.line.Begin() <= target_line;
				}
			}
			break;
		case PropertyType::Log:
			if (std::holds_alternative<std::u8string>(value))
			{
				std::u8string_view string_view = std::get<std::u8string>(value);
				switch (compare)
				{
				case CompareType::Bigger:
				case CompareType::BiggerEqual:
					return log.str.starts_with(string_view);
				case CompareType::Equal:
					return log.str.contains(string_view);
				case CompareType::Smaller:
				case CompareType::SmallerEqual:
					return log.str.ends_with(string_view);
				}
				return std::nullopt;
			}
			else if (std::holds_alternative<Potato::Reg::Dfa>(value))
			{
				Potato::Reg::Dfa const& reference = std::get<Potato::Reg::Dfa>(value);
				processor.Clear();
				processor.SetObserverTable(reference);
				auto accept = processor.Process(log.str);
				return accept;
			}
			break;
		case PropertyType::Category:

			break;
		case PropertyType::Time:
			break;
		}
		return std::nullopt;
	}

	std::optional<bool> OperatorStatement::Detect(LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor) const
	{
		auto s1 = statement_1->Detect(log, processor);
		if (s1.has_value())
		{
			if (is_or && *s1 || !is_or && !*s1)
			{
				return *s1;
			}
			auto s2 = statement_2->Detect(log, processor);
			if (s2.has_value())
			{
				return *s2;
			}
		}
		return std::nullopt;
	}

	std::unique_ptr<StatementInterface> LogFilterProcessor::ComplierStatement(std::u8string_view statement)
	{
		Potato::EBNF::EbnfProcessor pro;
		pro.SetObserverTable(GetEbnf(), [&](Potato::EBNF::SymbolInfo syminfo, std::size_t mask)->std::any {
			if (mask == 1)
			{
				std::size_t value = 0;
				Potato::Format::DirectDeformat(syminfo.TokenIndex.Slice(statement), value);
				return value;
			}
			else if (mask == 2)
			{
				std::u8string value{ syminfo.TokenIndex.Slice(statement) };
				return value;
			}
			else if (mask == 3)
			{
				Potato::Reg::Dfa dfa(Potato::Reg::Dfa::FormatE::HeadMatch, syminfo.TokenIndex.Slice(statement));
				return dfa;
			}
			else if (mask >= 4 && mask <= 8)
			{
				return static_cast<CompareType>(mask - 4);
			}else if(mask >= 10 && mask <= 16)
			{
				std::u8string value{ syminfo.TokenIndex.Slice(statement) };
				return value;
			}
			return std::any{};
			},
			[](Potato::EBNF::SymbolInfo Symbol, Potato::EBNF::ReduceProduction Production) -> std::any {
				volatile int i = 0;
				return std::any{};
			}
		);
		if (Potato::EBNF::Process(pro, statement))
		{
			return {};
		}
		return {};
	}

	void LogFilterProcessor::AddStatement(std::u8string_view statement)
	{
		auto node = ComplierStatement(statement);
		this->statement = std::move(node);
	}


	void Test()
	{
		std::u8string_view statement = std::u8string_view{ u8"$Level==Log && ($Line >= 1 || $Line <= 124);" };
		LogFilterProcessor processor;
		processor.AddStatement(statement);
		volatile int i = 0;
	}
}