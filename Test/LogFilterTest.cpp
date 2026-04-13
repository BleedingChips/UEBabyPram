import UEBabyPramLogParser;
import std;
import Potato;

std::u8string_view Source = 
u8R"(aaa
LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
[2021.10.11-11.53.12:082][178]LogConfig: Setting CVar [[con.DebugEarlyDefault:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
	sdasdasd
s	skdjaskldjasdkljklj
[2021.10.11-11.53.13:082][198]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
)";


int main()
{

	UEBabyPram::LogParser::ForeachLogLine(Source, [](UEBabyPram::LogParser::LogLine line){
		auto frame_count = line.GetFrameCount();
	});

	return 0;
}