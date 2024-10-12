import UEBabyPramLogFilter;
import std;

std::u8string_view Source = 
u8R"(LogConfig: Setting CVar [[net.AllowAsyncLoading:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Setting CVar [[con.DebugEarlyDefault:1]]
[2021.10.11-11.53.12:082][  0]LogConfig: Display: Setting CVar [[con.DebugEarlyDefault:1]]
)";


int main()
{

	UEBabyPram::LogFilter::ForeachLogLine(Source, [](UEBabyPram::LogFilter::LogLine Line){
		if (!Line.Time.empty())
		{
			auto Time = UEBabyPram::LogFilter::LogLineProcessor::GetTime(Line);
			volatile int i = 0;
		}
		volatile int i = 0;
	});

	return 0;
}