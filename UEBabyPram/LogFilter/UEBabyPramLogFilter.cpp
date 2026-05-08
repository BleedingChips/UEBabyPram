module;

module UEBabyPramLogFilter;
import Potato;

namespace UEBabyPram::LogFilter
{
	static constexpr std::u8string_view ebnf_string = u8R"(
		$:='\s+';
		INT:='[1-9][0-9]*' : [1];
		STR:='\^.*?[^\\]\^' : [2];
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

	std::optional<bool> ConditionStatement::Detect(std::span<ConditionStatement const> statemenets, LogParser::LogLine const& log, Potato::Reg::DfaProcessor& processor)
	{
		std::vector<std::size_t> condition_index;

		for (std::size_t i = 0; i < statemenets.size(); ++i)
		{
			auto& cur = statemenets[i];
			switch (cur.property)
			{
			case PropertyType::StatementOr:
				if (condition_index.size() >= 2)
				{
					auto s2 = *condition_index.rbegin();
					condition_index.pop_back();
					auto s1 = *condition_index.rbegin();
					condition_index.pop_back();
					if (
						s1 == std::numeric_limits<std::size_t>::max()
						|| s2 == std::numeric_limits<std::size_t>::max()
						)
					{
						condition_index.emplace_back(std::numeric_limits<std::size_t>::max());
					}
					else {
						if (s1 < std::numeric_limits<std::size_t>::max() - 1)
						{
							auto re = statemenets[s1].Detect(log, processor);
							if (re)
							{

							}
						}
					}
				}
				break;
			}
		}

		return std::nullopt;
	}


	void Test()
	{
		static Potato::EBNF::Ebnf ebnf(ebnf_string);
		auto binary_table = Potato::EBNF::CreateEbnfBinaryTable(ebnf);
		Potato::EBNF::EbnfBinaryTableWrapper wrapper({ binary_table.data(), binary_table.size()});
		std::pmr::vector<std::u8string_view> all_mask;
		Potato::EBNF::EbnfProcessor pro;
		pro.SetObserverTable(wrapper, [&](Potato::EBNF::SymbolInfo syminfo, std::size_t mask)->std::any {
			all_mask.emplace_back(
				syminfo.SymbolName
			);
			return std::any{};
			});
		bool re = Potato::EBNF::Process(pro, std::u8string_view{ u8"$Level==Log && ($Line >= 1 || $Line <= 124);" });
		
		Potato::Reg::Dfa dfa(Potato::Reg::Dfa::FormatE::GreedyHeadMatch, u8R"(\^.*?[^\\]\^)");
		Potato::Reg::DfaProcessor processer;
		processer.SetObserverTable(dfa);
		auto re3 = processer.Process(u8"^S1asas\\^^");
		std::int32_t i = 0;

		std::array<Potato::Encode::Unicode::CodePointT, 1024> code;
		std::array<std::size_t, 1024> index;
		auto info = Potato::Encode::UnicodeEncoder<char8_t, Potato::Encode::Unicode::CodePointT>
			::EncodeTo(u8"$Level==Log && ($Line >= 1 || $Line <= 124);", code, {}, index);

		auto span = std::span(code).subspan(0, info.target_space);
		auto index_span = std::span(index).subspan(0, info.source_space);

		Potato::EBNF::LexicalProcessor processor;
		processor.SetObserverTable(ebnf);

		std::array<Potato::EBNF::LexicalSymbol, 1024> out_symbol;
		//processor.Comsumed(span, index_span);



		volatile int i233 = 0;
	}
}