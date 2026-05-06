module;

module UEBabyPramLogFilter;
import Potato;

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

namespace UEBabyPram::LogFilter
{
	void Test()
	{
		Potato::EBNF::Ebnf ebnf(ebnf_string);
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