module;

module UEBabyPramLogFilter;
import Potato;

static constexpr std::u8string_view ebnf_string = u8R"(
$:='\s+';
INT:='[1-9][0-9]+' : [1];
STR:='\^.?[^\\]\^' : [2];
STR:='@\^.?[^\\]\^' : [3];
COMPARE:='<' : [4];
COMPARE:='<=' : [5];
COMPARE:='==' : [6];
COMPARE:='>' : [7];
COMPARE:='>=':[8];
%%%%
$:=<Exp>;
<TIME>:=INT '.' INT '.' INT ':' INT : [2];
<TIME>:=INT '.' INT '.' INT : [2];
<STAT>:='$Time' COMPARE <TIME> : [2];
<STAT>:='$Level' COMPARE <TIME> : [2];
<STAT>:='$Line' COMPARE INT : [2];
<STAT>:='$Log' '==' STR : [2];
<STAT>:='$Category' '==' STR : [2];
<STAT>:= '(' <STAT> ')' : [4];
<STAT>:= <STAT> '&&' <STAT>  : [4];
<STAT>:= <STAT> '||' <STAT>  : [4];
<Exp>:=<STAT> ';' :[4];
<Exp>:=;
%%%%
+('&&');
+('||');
)";

namespace UEBabyPram::LogFilter
{
	void Test()
	{
		Potato::EBNF::Ebnf ebnf(ebnf_string);
		std::pmr::vector<std::size_t> mask;
		Potato::EBNF::EbnfProcessor pro;
		pro.SetObserverTable(ebnf);
		Potato::EBNF::Process(pro, std::u8string_view{ u8"$Log==^S1asas^ && $Line >= 1;" });
		std::int32_t i = 0;
	}
}