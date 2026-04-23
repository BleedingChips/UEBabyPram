module;

module UEBabyPramLogFilter;
import Potato;

static constexpr std::u8string_view ebnf_string = u8R"(
$:='\s+';
CONDITION:='C[0-9]+' : [0];
INT:='[1-9][0-9]+' : [1];
COMPARE:='<' : [3];
COMPARE:='<=' : [4];
COMPARE:='==' : [5];
COMPARE:='>' : [6];
COMPARE:='>=':[7];
%%%%
$:=<Exp>
<TIME>:=INT '.' INT '.' INT ':' INT '.' INT '.' INT ':' INT : [1];
<STAT>:='$Time' '<' <TIME> : [2];
<Exp>:=<STAT> ';' :[4];
<Exp>:=;
)";

namespace UEBabyPram::LogFilter
{
	void Test()
	{
		Potato::EBNF::Ebnf ebnf(ebnf_string);
		std::int32_t i = 0;
	}
}