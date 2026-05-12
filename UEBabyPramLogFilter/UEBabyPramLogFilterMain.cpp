import UEBabyPram;
import UEBabyPramLogFilter;
import Potato;
import std;

constexpr auto log_filter = Potato::TMP::TypeString(u8"LogFilter");

namespace Potato::Log
{
	template<Potato::Log::LogLevel level>
	struct LogCategoryFormatter<log_filter, level>
	{
		template<typename OutputIterator, typename ...Parameters>
		OutputIterator operator()(OutputIterator iterator, std::basic_format_string<wchar_t, std::type_identity_t<Parameters>...> const& pattern, Parameters&& ...parameters)
		{
			if constexpr (level != Potato::Log::LogLevel::Display)
			{
				FormatedSystemTime time;
				iterator = std::format_to(
					std::move(iterator),
					L"[{}]{}<{}>",
					time, log_filter, level
				);
			}
			return std::format_to(
				std::move(iterator),
				pattern,
				std::forward<Parameters>(parameters)...
			);
		}
	};
}


using namespace UEBabyPram;
using namespace Potato;

int main(int argc, char* argv[])
{
	
	UEBabyPram::LogFilter::FilterSetting setting;

	for (std::size_t i = 0; i < argc; ++i)
	{
		if (argv[i] == "-f" || argv[i] == "--file")
		{
			if (i + 1 < argc)
			{

			}
			else {
				
			}
		}
	}
	Log::Log<log_filter, Log::LogLevel::Log, u8"Start Runing LogFilter">();
	return 0;
}